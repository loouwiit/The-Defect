/**
 * WebSocket 串流客户端 — 通过 ws://host:8080/ws/stream 获取并显示屏幕画面
 *
 * 协议: 请求-响应模式
 *   Client → Server: 任意 binary frame (触发抓帧)
 *   Server → Client: binary frame (JPEG 数据)
 */

(function () {
	'use strict';

	// ---- DOM 引用 ----
	const streamImage = document.getElementById('streamImage');
	const placeholder = document.getElementById('placeholder');
	const statusDot = document.getElementById('statusDot');
	const statusText = document.getElementById('statusText');
	const fpsDisplay = document.getElementById('fpsDisplay');
	const frameSizeDisplay = document.getElementById('frameSizeDisplay');
	const btnStart = document.getElementById('btnStart');
	const btnStop = document.getElementById('btnStop');

	// ---- 状态 ----
	let reconnectTimer = null;
	let manualStop = false;
	let currentObjectUrl = null;

	// FPS 统计（指数移动平均）
	let lastFrameTime = 0;
	let smoothedFps = 0;

	// ---- 旋转 Canvas（竖屏 JPEG → 横屏显示） ----
	// 面板物理分辨率 720×1280，JPEG 为竖屏。
	// 通过 Canvas 旋转 90° 后输出 1280×720 横屏画面。
	const ROTATE_CANVAS = document.createElement('canvas');
	ROTATE_CANVAS.width = 1280;
	ROTATE_CANVAS.height = 720;
	const ROTATE_CTX = ROTATE_CANVAS.getContext('2d');
	// 预配置变换：画布中心为原点，旋转 90°
	// （如果方向反了，把下面这行的 1 改成 -1）
	ROTATE_CTX.translate(640, 360);
	ROTATE_CTX.rotate(-1 * Math.PI / 2);

	// ---- 工具函数 ----

	function log(msg, type) {
		// removed
	}

	function setStatus(state, text) {
		statusDot.className = 'status-dot ' + state;
		statusText.textContent = text;
	}

	function displayFrame(jpegBytes) {
		// 断连后跳过过期帧，避免更新已清理的 DOM
		if (!streamActive && manualStop) return;

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

		const blob = new Blob([jpegBytes], { type: 'image/jpeg' });

		// 通过 Canvas 旋转竖屏 JPEG → 横屏显示
		createImageBitmap(blob).then(bitmap => {
			// 清除上一帧
			ROTATE_CTX.clearRect(-640, -360, 1280, 720);

			// 在旋转后的坐标系中绘制：
			// 图片 720×1280，以画布中心为原点，居中绘制
			ROTATE_CTX.drawImage(bitmap, -360, -640, 720, 1280);
			bitmap.close();

			// 将 Canvas 重新编码为 JPEG
			ROTATE_CANVAS.toBlob(rotatedBlob => {
				if (currentObjectUrl) {
					URL.revokeObjectURL(currentObjectUrl);
				}
				currentObjectUrl = URL.createObjectURL(rotatedBlob);

				if (placeholder && !placeholder.classList.contains('hidden')) {
					placeholder.classList.add('hidden');
				}

				streamImage.src = currentObjectUrl;
			}, 'image/jpeg', 0.82);
		}).catch(err => {
			// 降级：直接显示未旋转的图片
			console.warn('Canvas 旋转失败，降级显示:', err);
			if (currentObjectUrl) {
				URL.revokeObjectURL(currentObjectUrl);
			}
			currentObjectUrl = URL.createObjectURL(blob);
			if (placeholder && !placeholder.classList.contains('hidden')) {
				placeholder.classList.add('hidden');
			}
			streamImage.src = currentObjectUrl;
		});
	}

	// ---- WebSocket 串流 (ws://host:8080/ws/stream) ----
	const STREAM_WS_URL = 'ws://' + location.hostname + ':8080/ws/stream';

	let wsStream = null;
	let streamActive = false;

	async function startStream() {
		if (streamActive) return;

		if (reconnectTimer) {
			clearTimeout(reconnectTimer);
			reconnectTimer = null;
		}

		manualStop = false;
		lastFrameTime = 0;
		smoothedFps = 0;
		fpsDisplay.textContent = '--';
		frameSizeDisplay.textContent = '--';

		setStatus('connecting', '串流连接中...');
		btnStart.disabled = true;
		btnStop.disabled = false;

		wsStream = new WebSocket(STREAM_WS_URL);
		wsStream.binaryType = 'arraybuffer';

		// 保活定时器：定期发送触发帧，防止服务端推流任务异常退出后流永久死亡
		let keepAliveTimer = null;

		function startKeepAlive() {
			stopKeepAlive();
			keepAliveTimer = setInterval(() => {
				if (wsStream && wsStream.readyState === WebSocket.OPEN) {
					wsStream.send(new ArrayBuffer(0));
				}
			}, 3000);
		}

		function stopKeepAlive() {
			if (keepAliveTimer) {
				clearInterval(keepAliveTimer);
				keepAliveTimer = null;
			}
		}

		wsStream.onopen = function () {
			streamActive = true;
			setStatus('connected', '串流已连接');
			// 发送触发帧，服务端收到后开始自动推送画面
			wsStream.send(new ArrayBuffer(0));
			startKeepAlive();
		};

		wsStream.onmessage = function (e) {
			// 拷贝数据：new Uint8Array(e.data) 只是视图，底层 ArrayBuffer 可能在
			// 断连重连时被浏览器回收（detach），导致后续 createImageBitmap 报错
			// "An attempt was made to use an object that is not, or is no longer, usable"
			const raw = new Uint8Array(e.data);
			const copy = new Uint8Array(raw.length);
			copy.set(raw);
			displayFrame(copy);
		};

		wsStream.onclose = function () {
			streamActive = false;
			stopKeepAlive();
			wsStream = null;
			if (!manualStop) {
				setStatus('connecting', '重连中...');
				reconnectTimer = setTimeout(() => {
					reconnectTimer = null;
					startStream();
				}, 5000);
			} else {
				setStatus('disconnected', '串流未连接');
				btnStart.disabled = false;
				btnStop.disabled = true;
			}
		};

		wsStream.onerror = function () {
		};
	}


	function stopStream() {
		manualStop = true;
		if (reconnectTimer) {
			clearTimeout(reconnectTimer);
			reconnectTimer = null;
		}
		streamActive = false;
		if (wsStream) {
			wsStream.close();
			wsStream = null;
		}
		if (currentObjectUrl) {
			URL.revokeObjectURL(currentObjectUrl);
			currentObjectUrl = null;
		}
		setStatus('disconnected', '串流未连接');
		btnStart.disabled = false;
		btnStop.disabled = true;
	}

	// ---- 事件绑定 ----

	btnStart.addEventListener('click', startStream);
	btnStop.addEventListener('click', stopStream);

	window.addEventListener('beforeunload', function () {
		stopStream();
		stopTouchWS();
	});

	// ---- WebSocket 触控连接 (端口 8080) ----
	const TOUCH_WS_URL = 'ws://' + location.hostname + ':8080/ws/touch';

	let wsTouch = null;
	let wsTouchConnected = false;

	const touchStatusDot = document.getElementById('touchStatusDot');
	const touchStatusText = document.getElementById('touchStatusText');
	const btnTouchStart = document.getElementById('btnTouchStart');
	const btnTouchStop = document.getElementById('btnTouchStop');

	// 触控按钮的事件绑定 (必须在 const 声明之后)
	if (btnTouchStart) btnTouchStart.addEventListener('click', startTouchWS);
	if (btnTouchStop) btnTouchStop.addEventListener('click', stopTouchWS);

	function setTouchStatus(state, text) {
		if (touchStatusDot) touchStatusDot.className = 'status-dot ' + state;
		if (touchStatusText) touchStatusText.textContent = text;
	}

	function sendTouchBinary(x, y, type) {
		if (!wsTouch || wsTouch.readyState !== WebSocket.OPEN) return;
		const buf = new ArrayBuffer(12);
		const view = new DataView(buf);
		view.setInt32(0, x, true);   // 小端序
		view.setInt32(4, y, true);
		view.setInt32(8, type, true); // 0=down, 1=move, 2=up
		wsTouch.send(buf);
	}

	function startTouchWS() {
		if (wsTouch && wsTouch.readyState === WebSocket.OPEN) return;
		setTouchStatus('connecting', '触控连接中...');
		if (btnTouchStart) btnTouchStart.disabled = true;
		if (btnTouchStop) btnTouchStop.disabled = false;

		wsTouch = new WebSocket(TOUCH_WS_URL);
		wsTouch.binaryType = 'arraybuffer';

		wsTouch.onopen = function () {
			wsTouchConnected = true;
			setTouchStatus('connected', '触控已连接');
			log('触控 WebSocket 已连接', 'info');
		};

		wsTouch.onclose = function () {
			wsTouchConnected = false;
			wsTouch = null;
			setTouchStatus('disconnected', '触控未连接');
			if (btnTouchStart) btnTouchStart.disabled = false;
			if (btnTouchStop) btnTouchStop.disabled = true;
			log('触控 WebSocket 已断开', '');
		};

		wsTouch.onerror = function () {
			wsTouchConnected = false;
			setTouchStatus('disconnected', '触控错误');
			log('触控 WebSocket 错误', 'error');
		};
	}

	function stopTouchWS() {
		if (wsTouch) {
			wsTouch.close();
			wsTouch = null;
		}
		wsTouchConnected = false;
		setTouchStatus('disconnected', '触控未连接');
		if (btnTouchStart) btnTouchStart.disabled = false;
		if (btnTouchStop) btnTouchStop.disabled = true;
	}

	// ---- 触摸注入 (单点, 通过 WebSocket) ----
	const streamContainer = document.getElementById('streamContainer');
	const touchDot = document.getElementById('touchDot');
	let pointerActive = false;
	let pending = null;
	let rafScheduled = false;

	// 仅去重, 不限速率 (WebSocket 开销极低)
	let lastSentX = -1;
	let lastSentY = -1;

	function clientToDevice(clientX, clientY) {
		const rect = streamContainer.getBoundingClientRect();
		if (rect.width === 0 || rect.height === 0) return { x: 0, y: 0 };
		const relX = (clientX - rect.left) / rect.width;
		const relY = (clientY - rect.top) / rect.height;
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

	function updateDot(clientX, clientY) {
		if (!touchDot) return;
		const pos = dotPosRelative(clientX, clientY);
		touchDot.style.left = pos.left + 'px';
		touchDot.style.top = pos.top + 'px';
	}

	function sendPendingOnce() {
		rafScheduled = false;
		if (!pending) return;
		const p = pending;
		pending = null;

		// 释放事件(type=2)不受限
		if (p.type === 2) {
			lastSentX = lastSentY = -1;
			sendTouchBinary(p.x, p.y, p.type);
			return;
		}

		// 坐标无变化跳过
		if (p.x === lastSentX && p.y === lastSentY) return;

		lastSentX = p.x;
		lastSentY = p.y;
		sendTouchBinary(p.x, p.y, p.type);
	}

	function scheduleSend(x, y, type) {
		pending = { x, y, type };
		if (!rafScheduled) {
			rafScheduled = true;
			requestAnimationFrame(sendPendingOnce);
		}
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

	console.log('screen.js 已加载 — WebSocket 串流 + 触控');
})();
