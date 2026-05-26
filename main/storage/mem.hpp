#include "vfs.hpp"

namespace MemFileSystemError
{
	constexpr int Error = -1;
	constexpr int NoAviliableDescription = -2;
	constexpr int UnReachablePath = -3;
	constexpr int NotExistFile = -4;
	constexpr int NotOpenedFileDescription = -5;
	//constexpr int UnMatchedType = -6;
	constexpr int UnSupportOpreation = -7;
	constexpr int TooLongTheFile = -8;
}

constexpr size_t MaxMemFileDescriptionCount = 16;
constexpr size_t MemFileBlockTotolSize = 512;
constexpr size_t MemFileMaxSize = 6 * 1024 * 1024; // 6MB

bool mountMem();
// void testMem();
