/**
 * MJPEG 流客户端 — 通过 POST /api/screen/stream 获取并显示屏幕画面
 *
 * 协议格式 (multipart/x-mixed-replace):
 *   --FRAME\r\n
 *   Content-Type: image/jpeg\r\n
 *   Content-Length: <N>\r\n
 *   \r\n
 *   <N bytes JPEG>\r\n
 *   --FRAME\r\n
 *   ...
 */

(function () {
	'use strict';

	// ---- DOM 引用 ----
	const streamImage = document.getElementById('streamImage');
	const placeholder = document.getElementById('placeholder');
	const statusDot = document.getElementById('statusDot');
	const statusText = document.getElementById('statusText');
	const fpsDisplay = document.getElementById('fpsDisplay');
	const resolutionDisplay = document.getElementById('resolutionDisplay');
	const frameSizeDisplay = document.getElementById('frameSizeDisplay');
	const totalFramesDisplay = document.getElementById('totalFramesDisplay');
	const logEl = document.getElementById('log');
	const btnStart = document.getElementById('btnStart');
	const btnStop = document.getElementById('btnStop');

	// ---- 状态 ----
	let abortController = null;
	let running = false;
	let reconnectTimer = null;
	let manualStop = false;           // 区分手动停止和意外断线
	let totalFrames = 0;
	let currentObjectUrl = null;

	// FPS 统计（指数移动平均）
	let lastFrameTime = 0;
	let smoothedFps = 0;

	// ---- 工具函数 ----

	function log(msg, type) {
		logEl.textContent = msg;
		logEl.className = type || '';
	}

	function setStatus(state, text) {
		statusDot.className = 'status-dot ' + state;
		statusText.textContent = text;
	}

	function displayFrame(jpegBytes) {
		totalFrames++;
		totalFramesDisplay.textContent = totalFrames;
		frameSizeDisplay.textContent = (jpegBytes.length / 1024).toFixed(1) + ' KB';

		// FPS 计算（指数移动平均）
		const now = performance.now();
		if (lastFrameTime > 0) {
			const dt = now - lastFrameTime;
			if (dt > 0) {
				const instantFps = 1000 / dt;
				smoothedFps = smoothedFps > 0
					? smoothedFps * 0.9 + instantFps * 0.1
					: instantFps;
				fpsDisplay.textContent = smoothedFps.toFixed(1);
			}
		}
		lastFrameTime = now;

		// 释放上一帧 blob URL
		if (currentObjectUrl) {
			URL.revokeObjectURL(currentObjectUrl);
		}

		const blob = new Blob([jpegBytes], { type: 'image/jpeg' });
		currentObjectUrl = URL.createObjectURL(blob);

		if (placeholder && !placeholder.classList.contains('hidden')) {
			placeholder.classList.add('hidden');
		}

		requestAnimationFrame(() => {
			streamImage.src = currentObjectUrl;
		});
	}

	// ---- MJPEG 流解析器 ----

	/**
	 * 缓冲读取器：从 ReadableStream 读取数据，高效处理分块
	 */
	class StreamParser {
		constructor(reader) {
			this.reader = reader;
			this.chunks = [];
			this.totalLen = 0;
		}

		_flatten() {
			const buf = new Uint8Array(this.totalLen);
			let offset = 0;
			for (const c of this.chunks) {
				buf.set(c, offset);
				offset += c.byteLength;
			}
			return buf;
		}

		search(pos, seq) {
			const flat = this._flatten();
			const end = flat.length - seq.length;
			for (let i = pos; i <= end; i++) {
				let match = true;
				for (let j = 0; j < seq.length; j++) {
					if (flat[i + j] !== seq[j]) { match = false; break; }
				}
				if (match) return i;
			}
			return -1;
		}

		readText(pos, len) {
			return new TextDecoder().decode(this._flatten().slice(pos, pos + len));
		}

		discard(n) {
			if (n <= 0) return;
			let remaining = n;
			while (remaining > 0 && this.chunks.length > 0) {
				const first = this.chunks[0];
				if (first.byteLength <= remaining) {
					remaining -= first.byteLength;
					this.totalLen -= first.byteLength;
					this.chunks.shift();
				} else {
					this.chunks[0] = first.slice(remaining);
					this.totalLen -= remaining;
					remaining = 0;
				}
			}
		}

		push(data) {
			this.chunks.push(data);
			this.totalLen += data.byteLength;
		}

		extractJpeg(start, end) {
			return this._flatten().slice(start, end);
		}
	}

	// 预编码的搜索模式
	const BOUNDARY = new TextEncoder().encode('--FRAME\r\n');
	const HEADER_END = new TextEncoder().encode('\r\n\r\n');
	const CRLF = new TextEncoder().encode('\r\n');

	const STATE = { SEARCH: 0, HEADER: 1, BODY: 2 };

	async function readStream(reader) {
		const parser = new StreamParser(reader);
		let state = STATE.SEARCH;
		let contentLength = -1;
		let bodyStart = 0;

		while (running) {
			const { done, value } = await reader.read();
			if (done) { log('流已结束', ''); break; }
			parser.push(value);

			let progressed = true;
			while (progressed) {
				progressed = false;

				if (state === STATE.SEARCH) {
					const pos = parser.search(0, BOUNDARY);
					if (pos >= 0) {
						parser.discard(pos + BOUNDARY.length);
						state = STATE.HEADER;
						progressed = true;
					}
				}

				if (state === STATE.HEADER) {
					const pos = parser.search(0, HEADER_END);
					if (pos >= 0) {
						const text = parser.readText(0, pos);
						const m = text.match(/content-length:\s*(\d+)/i);
						if (!m) {
							log('缺少 Content-Length，跳过', 'error');
							parser.discard(pos + HEADER_END.length);
							state = STATE.SEARCH;
							progressed = true;
							continue;
						}
						contentLength = parseInt(m[1], 10);
						bodyStart = pos + HEADER_END.length;
						state = STATE.BODY;
						progressed = true;
					}
				}

				if (state === STATE.BODY) {
					const bodyEnd = bodyStart + contentLength;
					if (parser.totalLen >= bodyEnd) {
						const jpeg = parser.extractJpeg(bodyStart, bodyEnd);
						setTimeout(() => displayFrame(jpeg), 0);
						parser.discard(bodyEnd + CRLF.length);
						contentLength = -1;
						state = STATE.SEARCH;
						progressed = true;
					}
				}
			}
		}
	}

	// ---- 启动 / 停止 ----

	async function startStream() {
		if (running) return;

		if (reconnectTimer) {
			clearTimeout(reconnectTimer);
			reconnectTimer = null;
		}

		manualStop = false;
		running = true;
		abortController = new AbortController();
		totalFrames = 0;
		lastFrameTime = 0;
		smoothedFps = 0;
		totalFramesDisplay.textContent = '0';
		fpsDisplay.textContent = '--';
		frameSizeDisplay.textContent = '--';
		resolutionDisplay.textContent = '--';

		setStatus('connecting', '连接中...');
		btnStart.disabled = true;
		btnStop.disabled = false;
		log('正在连接 /api/screen/stream...', '');

		try {
			const response = await fetch('/api/screen/stream', {
				method: 'POST',
				signal: abortController.signal,
			});
			if (!response.ok) {
				throw new Error('HTTP ' + response.status);
			}
			setStatus('connected', '已连接');
			log('已连接到 MJPEG 流', 'info');
			resolutionDisplay.textContent = 'MJPEG 流';

			if (!response.body) {
				throw new Error('浏览器不支持 ReadableStream');
			}
			await readStream(response.body.getReader());

		} catch (err) {
			if (err.name === 'AbortError') {
				log('串流已停止', '');
			} else {
				log('连接失败: ' + err.message, 'error');
				console.error('Stream error:', err);
			}
		} finally {
			running = false;
			btnStart.disabled = false;
			btnStop.disabled = true;

			if (!manualStop && !reconnectTimer) {
				log('5 秒后自动重连...', '');
				setStatus('connecting', '重连中...');
				reconnectTimer = setTimeout(() => {
					reconnectTimer = null;
					startStream();
				}, 5000);
			} else if (!manualStop) {
				setStatus('disconnected', '未连接');
			}
		}
	}

	function stopStream() {
		manualStop = true;
		if (reconnectTimer) {
			clearTimeout(reconnectTimer);
			reconnectTimer = null;
		}
		running = false;
		if (abortController) {
			abortController.abort();
			abortController = null;
		}
		if (currentObjectUrl) {
			URL.revokeObjectURL(currentObjectUrl);
			currentObjectUrl = null;
		}
		setStatus('disconnected', '已停止');
		btnStart.disabled = false;
		btnStop.disabled = true;
		log('串流已停止', '');
	}

	// ---- 事件绑定 ----

	btnStart.addEventListener('click', startStream);
	btnStop.addEventListener('click', stopStream);

	window.addEventListener('beforeunload', function () {
		if (abortController) abortController.abort();
		if (currentObjectUrl) URL.revokeObjectURL(currentObjectUrl);
	});

	// ---- 触摸注入 (单点) ----
	const streamContainer = document.getElementById('streamContainer');
	const touchDot = document.getElementById('touchDot');
	let pointerActive = false;
	let pending = null;
	let rafScheduled = false;

	// 发包速率限制: 最小间隔 100ms (~10次/秒), 坐标无变化跳过
	const TOUCH_MIN_INTERVAL_MS = 100;
	let lastSentTime = 0;
	let lastSentX = -1;
	let lastSentY = -1;

	function clientToDevice(clientX, clientY) {
		// 用 container rect 而非 streamImage, 避免 object-fit: contain 引起的坐标偏移
		const rect = streamContainer.getBoundingClientRect();
		if (rect.width === 0 || rect.height === 0) return { x: 0, y: 0 };
		const relX = (clientX - rect.left) / rect.width;
		const relY = (clientY - rect.top) / rect.height;
		// clamp
		const cx = Math.min(Math.max(relX, 0), 1);
		const cy = Math.min(Math.max(relY, 0), 1);
		// device resolution (横屏 1280×720)
		const x = Math.round(cx * 1280);
		const y = Math.round(cy * 720);
		return { x, y };
	}

	function dotPosRelative(clientX, clientY) {
		const containerRect = streamContainer.getBoundingClientRect();
		return {
			left: clientX - containerRect.left,
			top: clientY - containerRect.top
		};
	}

	function sendPendingOnce() {
		rafScheduled = false;
		if (!pending) return;
		const p = pending;
		pending = null;

		const now = performance.now();
		const samePos = (p.x === lastSentX && p.y === lastSentY);
		const tooSoon = (now - lastSentTime < TOUCH_MIN_INTERVAL_MS);

		// 释放事件(type=2)不受限, 确保能触发
		if (p.type === 2) {
			lastSentTime = now;
			lastSentX = lastSentY = -1;
			fetch('/api/screen/touch', { method: 'POST', body: `${p.x},${p.y},${p.type}` }).catch(()=>{});
			return;
		}

		if (samePos || tooSoon) return;

		lastSentTime = now;
		lastSentX = p.x;
		lastSentY = p.y;
		fetch('/api/screen/touch', { method: 'POST', body: `${p.x},${p.y},${p.type}` }).catch(()=>{});
	}

	function scheduleSend(x, y, type) {
		pending = { x, y, type };
		if (!rafScheduled) {
			rafScheduled = true;
			requestAnimationFrame(sendPendingOnce);
		}
	}

	function updateDot(clientX, clientY) {
		if (!touchDot) return;
		const pos = dotPosRelative(clientX, clientY);
		touchDot.style.left = pos.left + 'px';
		touchDot.style.top = pos.top + 'px';
	}

	streamContainer.addEventListener('pointerdown', (e) => {
		e.preventDefault();
		pointerActive = true;
		streamContainer.setPointerCapture(e.pointerId);
		const d = clientToDevice(e.clientX, e.clientY);
		scheduleSend(d.x, d.y, 0);
		if (touchDot) touchDot.style.display = 'block';
		updateDot(e.clientX, e.clientY);
	});

	streamContainer.addEventListener('pointermove', (e) => {
		if (!pointerActive) return;
		e.preventDefault();
		const d = clientToDevice(e.clientX, e.clientY);
		scheduleSend(d.x, d.y, 1);
		updateDot(e.clientX, e.clientY);
	});

	streamContainer.addEventListener('pointerup', (e) => {
		if (!pointerActive) return;
		pointerActive = false;
		streamContainer.releasePointerCapture(e.pointerId);
		const d = clientToDevice(e.clientX, e.clientY);
		scheduleSend(d.x, d.y, 2);
		if (touchDot) touchDot.style.display = 'none';
	});

	streamContainer.addEventListener('pointercancel', (e) => {
		if (!pointerActive) return;
		pointerActive = false;
		scheduleSend(0, 0, 2);
		if (touchDot) touchDot.style.display = 'none';
	});

	console.log('screen.js 已加载 — MJPEG 流客户端');
})();
