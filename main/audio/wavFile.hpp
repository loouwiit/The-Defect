#pragma once

#include <cstdint>
#include <cstring>

#include "storage/fat.hpp"

/**
 * Wav — 最小 WAV (RIFF / PCM) 头读写工具
 *
 * 只处理标准 44 字节头（RIFF + fmt(16) + data），即单声道/立体声 PCM。
 * 录音流程：先写占位头（dataBytes = 0）→ 顺序追加 PCM → 停止时回填长度。
 *
 * ⚠ `OFile::open` 为 `O_RDWR | O_CREAT`（无 `O_TRUNC`），写前必须 `removeFile`，
 *   否则残留旧数据的长度会被 `size = max(size, ...)` 保留下来。
 */
namespace Wav
{
	constexpr size_t HeaderSize = 44;

	struct Info
	{
		uint32_t sampleRate{};
		uint16_t channels{};
		uint16_t bitsPerSample{};
		uint32_t dataBytes{};
	};

	inline void putLE16(uint8_t* p, uint16_t v)
	{
		p[0] = (uint8_t)(v & 0xFF);
		p[1] = (uint8_t)((v >> 8) & 0xFF);
	}

	inline void putLE32(uint8_t* p, uint32_t v)
	{
		p[0] = (uint8_t)(v & 0xFF);
		p[1] = (uint8_t)((v >> 8) & 0xFF);
		p[2] = (uint8_t)((v >> 16) & 0xFF);
		p[3] = (uint8_t)((v >> 24) & 0xFF);
	}

	inline uint16_t getLE16(const uint8_t* p)
	{
		return (uint16_t)(p[0] | (p[1] << 8));
	}

	inline uint32_t getLE32(const uint8_t* p)
	{
		return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	}

	/** 生成标准 PCM WAV 头（dataBytes 可先填 0，录完用 updateSizes 回填） */
	inline void buildHeader(uint8_t* h, uint32_t sampleRate, uint16_t channels,
	                        uint16_t bitsPerSample, uint32_t dataBytes)
	{
		const uint16_t blockAlign = (uint16_t)(channels * (bitsPerSample / 8));
		const uint32_t byteRate = sampleRate * blockAlign;

		memcpy(h, "RIFF", 4);
		putLE32(h + 4, 36 + dataBytes);
		memcpy(h + 8, "WAVE", 4);
		memcpy(h + 12, "fmt ", 4);
		putLE32(h + 16, 16);            // fmt 块长度
		putLE16(h + 20, 1);             // PCM
		putLE16(h + 22, channels);
		putLE32(h + 24, sampleRate);
		putLE32(h + 28, byteRate);
		putLE16(h + 32, blockAlign);
		putLE16(h + 34, bitsPerSample);
		memcpy(h + 36, "data", 4);
		putLE32(h + 40, dataBytes);
	}

	/** 写入 44 字节头（文件偏移推进 44，后续可直接追加 PCM） */
	inline bool writeHeader(OFile& file, uint32_t sampleRate, uint16_t channels,
	                        uint16_t bitsPerSample, uint32_t dataBytes = 0)
	{
		uint8_t h[HeaderSize];
		buildHeader(h, sampleRate, channels, bitsPerSample, dataBytes);
		return file.write(h, HeaderSize) == HeaderSize;
	}

	/** 回填 RIFF/data 长度（停止录音后调用，随后可安全关闭文件） */
	inline bool updateSizes(OFile& file, uint32_t dataBytes)
	{
		uint8_t buf[4];

		putLE32(buf, 36 + dataBytes);
		file.setOffset(4, FileBase::OffsetMode::Begin);
		if (file.write(buf, 4) != 4) return false;

		putLE32(buf, dataBytes);
		file.setOffset(40, FileBase::OffsetMode::Begin);
		if (file.write(buf, 4) != 4) return false;

		return true;
	}

	/** 解析头部（校验 RIFF/WAVE/data 标识） */
	inline bool readInfo(IFile& file, Info& info)
	{
		uint8_t h[HeaderSize];
		file.setOffset(0, FileBase::OffsetMode::Begin);
		if (file.read(h, HeaderSize) != HeaderSize) return false;
		if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0 ||
		    memcmp(h + 36, "data", 4) != 0) {
			return false;
		}

		info.channels      = getLE16(h + 22);
		info.sampleRate    = getLE32(h + 24);
		info.bitsPerSample = getLE16(h + 34);
		info.dataBytes     = getLE32(h + 40);
		return true;
	}
}
