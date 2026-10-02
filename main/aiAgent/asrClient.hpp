#pragma once

#include <functional>
#include <string>

#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>

#include "aiAgent/aiConfig.hpp"
#include "storage/fat.hpp"
#include "task/task.hpp"

/**
 * @file asrClient.hpp
 * @brief 云端语音识别客户端（OpenAI Whisper 兼容）
 *
 * 以 multipart/form-data 流式上传 WAV 文件（不整读进内存），
 * 解析响应 `{"text":"..."}`。
 *
 * 使用：
 * @code{cpp}
 *   AiConfig cfg = aiConfigLoad();
 *   AsrClient::transcribeFile(cfg, "/root/mem/voice.wav", "zh",
 *       [](const AsrResult& r) {
 *           if (r.ok) { ... r.text ... } else { ... r.error ... }
 *       });
 * @endcode
 *
 * 说明：
 * - 端点：`{asrUrl|apiUrl}` + `{asrPath}`（默认 `/v1/audio/transcriptions`；**本地 crispasr 服务用 `/inference`**）
 * - 配置：`/root/system/ai_config.json` 的 `asr_url` / `asr_path` / `asr_model` / `asr_language`
 * - 上传缓冲 / 响应缓冲均取自 PSRAM
 * - 响应与 OpenAI Whisper 一致：顶层 `text` 字段（crispasr 亦满足，可直接对接）
 */

struct AsrResult
{
	bool        ok{};
	std::string text;   //!< 识别文本
	std::string error;  //!< 失败原因
};

using AsrCallback = std::function<void(const AsrResult&)>;

class AsrClient
{
public:
	constexpr static char TAG[] = "AsrClient";

	/** @brief 异步转写 WAV 文件（回调在 Task 线程执行） */
	static bool transcribeFile(const AiConfig& config, const std::string& wavPath,
	                           const std::string& language, AsrCallback callback)
	{
		auto* ctx = new Ctx{ config, wavPath, language, std::move(callback) };
		if (ctx == nullptr) {
			return false;
		}
		Task::addTask([](void* param) -> TickType_t {
			auto* c = static_cast<Ctx*>(param);
			AsrResult result = execute(*c);
			if (c->callback) {
				c->callback(result);
			}
			delete c;
			return Task::infinityTime;
		}, "asrClient", ctx, 0, Task::Affinity::None);
		return true;
	}

private:
	struct Ctx
	{
		AiConfig    config;
		std::string path;
		std::string language;
		AsrCallback callback;
	};

	/** @brief 完整写入（处理部分发送；返回实际写入字节数） */
	static size_t writeAll(esp_http_client_handle_t client, const void* data, size_t len)
	{
		const uint8_t* p = static_cast<const uint8_t*>(data);
		size_t sent = 0;
		while (sent < len) {
			int chunk = (len - sent > 4096) ? 4096 : (int)(len - sent);
			int n = esp_http_client_write(client, (const char*)p + sent, chunk);
			if (n <= 0) {
				break;
			}
			sent += (size_t)n;
		}
		return sent;
	}

	static AsrResult execute(const Ctx& ctx)
	{
		AsrResult res;

		if (!ctx.config.valid) {
			res.error = "AI 未配置（缺少 API Key）";
			return res;
		}

		// ── 1. 打开 WAV 文件取大小 ──
		IFile file;
		if (!file.open(ctx.path.c_str())) {
			res.error = "无法打开录音文件";
			return res;
		}
		file.reGetSize();
		off_t fileSize = file.getSize();
		if (fileSize <= 0) {
			res.error = "录音文件为空";
			return res;
		}

		// ── 2. 组装 multipart 头尾 ──
		const std::string boundary = "----TheDefectAsrBoundary";
		const std::string model = ctx.config.asrModel.empty() ? "whisper-1" : ctx.config.asrModel;
		const std::string lang = ctx.language.empty() ? ctx.config.asrLanguage : ctx.language;

		std::string preamble;
		preamble += "--" + boundary + "\r\n";
		preamble += "Content-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\n";
		preamble += "Content-Type: audio/wav\r\n\r\n";

		std::string epilogue;
		epilogue += "\r\n--" + boundary + "\r\n";
		epilogue += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
		epilogue += model + "\r\n";
		if (!lang.empty()) {
			epilogue += "--" + boundary + "\r\n";
			epilogue += "Content-Disposition: form-data; name=\"language\"\r\n\r\n";
			epilogue += lang + "\r\n";
		}
		epilogue += "--" + boundary + "--\r\n";

		const size_t total = preamble.size() + (size_t)fileSize + epilogue.size();

		// ── 3. URL ──
		std::string url = ctx.config.asrUrl.empty() ? ctx.config.apiUrl : ctx.config.asrUrl;
		while (!url.empty() && url.back() == '/') url.pop_back();
		// 路径可配置：OpenAI 兼容端点用 /v1/audio/transcriptions，本地 crispasr 服务用 /inference
		const std::string path = ctx.config.asrPath.empty()
			? std::string("/v1/audio/transcriptions")
			: ctx.config.asrPath;
		url += (path[0] == '/') ? path : ("/" + path);

		// ── 4. HTTP 客户端 ──
		esp_http_client_config_t cfg = {};
		cfg.url = url.c_str();
		cfg.method = HTTP_METHOD_POST;
		cfg.timeout_ms = ctx.config.timeoutMs;
		cfg.buffer_size = 8192;
		cfg.buffer_size_tx = 1024;
		cfg.crt_bundle_attach = esp_crt_bundle_attach;

		esp_http_client_handle_t client = esp_http_client_init(&cfg);
		if (client == nullptr) {
			res.error = "esp_http_client_init 失败";
			return res;
		}

		const std::string contentType = "multipart/form-data; boundary=" + boundary;
		const std::string auth = "Bearer " + ctx.config.apiKey;
		esp_http_client_set_header(client, "Content-Type", contentType.c_str());
		esp_http_client_set_header(client, "Authorization", auth.c_str());

		char* respBuf = nullptr;
		char* ioBuf = nullptr;

		ESP_LOGI(TAG, "POST %s (WAV %ld 字节, 共 %u 字节)", url.c_str(),
			(long)fileSize, (unsigned)total);

		if (esp_http_client_open(client, (int)total) != ESP_OK) {
			res.error = "HTTP 连接失败";
			goto cleanup;
		}

		// ── 5. 写 preamble ──
		if (writeAll(client, preamble.data(), preamble.size()) != preamble.size()) {
			res.error = "上传 multipart 头失败";
			goto cleanup;
		}

		// ── 6. 流式写文件内容（分块，避免整读内存） ──
		{
			constexpr size_t IoBufSize = 4096;
			ioBuf = static_cast<char*>(heap_caps_malloc(IoBufSize, MALLOC_CAP_SPIRAM));
			if (ioBuf == nullptr) {
				res.error = "内存不足";
				goto cleanup;
			}

			file.setOffset(0, FileBase::OffsetMode::Begin);
			off_t sent = 0;
			while (sent < fileSize) {
				size_t want = IoBufSize;
				if ((off_t)want > fileSize - sent) {
					want = (size_t)(fileSize - sent);
				}
				size_t got = file.read(ioBuf, want);
				if (got == 0) {
					break;
				}
				if (writeAll(client, ioBuf, got) != got) {
					res.error = "上传音频数据失败";
					goto cleanup;
				}
				sent += got;
			}
			if (sent != fileSize) {
				res.error = "录音文件读取不完整";
				goto cleanup;
			}
		}

		// ── 7. 写 epilogue ──
		if (writeAll(client, epilogue.data(), epilogue.size()) != epilogue.size()) {
			res.error = "上传 multipart 尾失败";
			goto cleanup;
		}

		// ── 8. 读响应 ──
		{
			int contentLen = esp_http_client_fetch_headers(client);
			int status = esp_http_client_get_status_code(client);
			ESP_LOGI(TAG, "HTTP %d, Content-Length: %d", status, contentLen);

			size_t bufSize = (contentLen > 0) ? (size_t)contentLen + 1 : 8192;
			respBuf = static_cast<char*>(heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM));
			if (respBuf == nullptr) {
				res.error = "内存不足";
				goto cleanup;
			}

			size_t offset = 0;
			int readLen = 0;
			while ((readLen = esp_http_client_read(client, respBuf + offset,
			                                       (int)(bufSize - offset - 1))) > 0) {
				offset += readLen;
				respBuf[offset] = '\0';
				if (bufSize - offset < 1024) {
					size_t newSize = bufSize * 2;
					char* newBuf = static_cast<char*>(
						heap_caps_realloc(respBuf, newSize, MALLOC_CAP_SPIRAM));
					if (newBuf == nullptr) {
						break;
					}
					respBuf = newBuf;
					bufSize = newSize;
				}
			}
			if (offset == 0) {
				respBuf[0] = '\0';
			}

			if (status != 200) {
				res.error = "识别接口返回 ";
				res.error += std::to_string(status);
				if (offset > 0) {
					res.error += ": ";
					res.error += respBuf;
				}
				goto cleanup;
			}
			if (offset == 0) {
				res.error = "识别接口响应为空";
				goto cleanup;
			}

			ESP_LOGI(TAG, "响应体: %s", respBuf);

			cJSON* root = cJSON_Parse(respBuf);
			if (root == nullptr) {
				res.error = "识别响应 JSON 解析失败";
				goto cleanup;
			}
			cJSON* text = cJSON_GetObjectItem(root, "text");
			if (cJSON_IsString(text) && text->valuestring != nullptr) {
				res.text = text->valuestring;
				res.ok = true;
			}
			else {
				cJSON* errObj = cJSON_GetObjectItem(root, "error");
				cJSON* errMsg = errObj ? cJSON_GetObjectItem(errObj, "message") : nullptr;
				res.error = (cJSON_IsString(errMsg) && errMsg->valuestring)
					? errMsg->valuestring
					: "识别响应中缺少 text 字段";
			}
			cJSON_Delete(root);
		}

	cleanup:
		if (respBuf) heap_caps_free(respBuf);
		if (ioBuf) heap_caps_free(ioBuf);
		if (client) esp_http_client_cleanup(client);

		// 去掉首尾空白（识别结果常带前后空格）
		if (res.ok) {
			const char* ws = " \t\r\n";
			size_t b = res.text.find_first_not_of(ws);
			if (b == std::string::npos) {
				res.text.clear();
			}
			else {
				size_t e = res.text.find_last_not_of(ws);
				res.text = res.text.substr(b, e - b + 1);
			}
			if (res.text.empty()) {
				res.ok = false;
				res.error = "未识别到语音内容";
			}
		}
		if (!res.ok) {
			ESP_LOGW(TAG, "识别失败: %s", res.error.c_str());
		}
		else {
			ESP_LOGI(TAG, "识别结果: %s", res.text.c_str());
		}
		return res;
	}
};
