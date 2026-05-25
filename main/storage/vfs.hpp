#pragma once
#include <sys/dirent.h>

constexpr char PerfixRoot[] = "/root";

constexpr char PerfixFlash[] = "/root";
constexpr char PerfixMem[] = "/root/mem";
constexpr char PrefixSd[] = "/root/sd";

using FileTypeBase = uint8_t;
enum class FileType : FileTypeBase
{
	File = DT_REG,
	Floor = DT_DIR,
	Both = File | Floor,
};
