#include <algorithm>

#include <esp_vfs.h>

#include <fcntl.h>

#include "mutex/mutex.hpp"
#include "utility/stringCompare.hpp"
#include "utility/nonCopyAble.hpp"
#include "mem.hpp"
#include "fat.hpp"

using std::min;
using std::max;

#define MemDebug false
#define MemUsageDebug false
#define MemProcessDebug false

template <class Child>
class MemBlockLinker; //链表
class MemFileDataBlock; // 数据块
class MemDirent; // VFS兼容
class MemFileHead; // 文件头，提供存储
class MemFileView; // 文件视口，提供读写
class MemFloorHead; // 目录头
class MemFloorDir; // VFS兼容

// linker:链表
template <class Child>
class MemBlockLinker : public NonCopyAble
{
public:
	//内部文件，直接全部public
	Child* last = nullptr;
	Child* next = nullptr;
};

// dataBlock:数据块
class MemFileDataBlock : public MemBlockLinker<MemFileDataBlock>
{
public:
	//内部文件，直接全部public
	constexpr static size_t BlockDataSize = MemFileBlockTotolSize - sizeof(MemBlockLinker);
	char data[BlockDataSize]{};
};
static_assert(sizeof(MemFileDataBlock) == MemFileBlockTotolSize, "MemFileDataBlock大小异常");

// dirent VFS兼容文件
class MemDirent
{
public:
	//内部文件，直接全部public
	struct dirent dirent;
	char(&name)[256] = dirent.d_name;
	size_t nameLenght = 0;
};

// fileHead:文件头
class MemFileHead : public MemBlockLinker<MemFileHead>, public MemDirent
{
public:
	//内部文件，直接全部public
	struct stat st {};
	off_t& size = st.st_size;

	MemFileDataBlock* dataBlock = nullptr;

	MemFloorHead* parent = nullptr;

	MemFileHead();
	~MemFileHead();
};

// fileView:文件窗口
class MemFileView
{
public:
	//内部文件，直接全部public
	MemFileHead* file = nullptr;

	MemFileDataBlock* block;
	off_t blockOffset = 0;
	off_t localOffset = 0;

	size_t write(const char* buffer, size_t size);
	size_t read(char* buffer, size_t size);

	void applyLocalOffset();
};

// floorHead:目录头
class MemFloorHead : public MemBlockLinker<MemFloorHead>, public MemDirent
{
public:
	//内部文件，直接全部public
	MemFloorHead* parent = nullptr;
	MemFloorHead* child = nullptr; //first child
	MemFileHead* file = nullptr;

	MemFloorHead();
	~MemFloorHead();
	MemFloorHead* findChildFloor(const char* floorName, size_t floorLenght);
	MemFloorHead* findFloor(const char* path, size_t pathLenght);
	MemFileHead* findChildFile(const char* fileName, size_t fileNameLenght);
	MemFileHead* findFile(const char* path, size_t pathLenght);

	bool removeChildFloor(const char* floorName, size_t floorNameLenght);
	bool removeFloor(const char* path, size_t pathLenght);
	bool removeChildFile(const char* fileName, size_t fileNameLenght);
	bool removeFile(const char* path, size_t pathLenght);

	bool makeChildFloor(const char* floorName, size_t floorNameLenght);
	bool makeFloor(const char* path, size_t pathLenght);
	bool makeChildFile(const char* fileName, size_t fileNameLenght);
	bool makeFile(const char* path, size_t pathLenght);
};

// floorDir VFS兼容
class MemFloorDir
{
public:
	DIR _RamDefender;
	MemFloorHead* floor = nullptr;
	MemFloorHead* operatorChild = nullptr;
	MemFileHead* operatorFile = nullptr;
	size_t operatorCount = 0;
};

ssize_t memWrite(int fd, const void* data, size_t size);
int memOpen(const char* path, int flags, int mode);
int memFstat(int fd, struct stat* st);
int memStat(const char* path, struct stat* st);
int memClose(int fd);
ssize_t memRead(int fd, void* dst, size_t size);
off_t memSeek(int fd, off_t size, int mod);
int memRename(const char* src, const char* dst);
int memUnlink(const char* path);
DIR* memOpenDir(const char* name);
dirent* memReadDir(DIR* pdir);
long memTellDir(DIR* pdir);
void memSeekDir(DIR* pdir, long offset);
int memCloseDir(DIR* pdir);
int memMakeDir(const char* name, mode_t mode);
int memRemoveDir(const char* name);

#if MemDebug && MemUsageDebug
EXT_RAM_BSS_ATTR size_t memTotolUsage = 0;
#endif
EXT_RAM_BSS_ATTR MemFloorHead memRoot;
EXT_RAM_BSS_ATTR MemFileView memFileDescriptions[MaxMemFileDescriptionCount] = {};
EXT_RAM_BSS_ATTR Mutex mutex;

// class函数

MemFileHead::MemFileHead()
{
	dirent.d_type = (unsigned char)FileType::File;
	dirent.d_name[0] = '\0';
	st.st_mode = _IFREG;
	st.st_size = 0;
}

MemFileHead::~MemFileHead()
{
	this->parent = nullptr;

	// 逐个删除
	if (dataBlock != nullptr) while (dataBlock->next != nullptr)
	{
		dataBlock = dataBlock->next;

#if MemDebug && MemUsageDebug
		memTotolUsage -= sizeof(MemFileDataBlock);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		delete dataBlock->last;
	}

#if MemDebug && MemUsageDebug
	memTotolUsage -= sizeof(MemFileDataBlock);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

	delete dataBlock;
	dataBlock = nullptr;
}

size_t MemFileView::write(const char* buffer, size_t size)
{
	// 保证不超长
	if (this->file->size + size > MemFileMaxSize)
		return MemFileSystemError::TooLongTheFile;

	auto sizeParamRecord = size; // 仅仅是记录

	//确保有块
	if (block == nullptr)
	{

#if MemDebug && MemUsageDebug
		memTotolUsage += sizeof(MemFileDataBlock);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		file->dataBlock = block = new MemFileDataBlock;
	}

	// 应用偏移
	this->applyLocalOffset();

	// 写入起始块
	size_t writeSize = min(MemFileDataBlock::BlockDataSize - (size_t)localOffset, size);
	memcpy(block->data + localOffset, buffer, writeSize);
	localOffset += writeSize;
	buffer += writeSize;
	size -= writeSize;

	// 写入中间完整块
	while (size > MemFileDataBlock::BlockDataSize)
	{

		if (block->next == nullptr)
		{
#if MemDebug && MemUsageDebug
			memTotolUsage += sizeof(MemFileDataBlock);
			printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif
			block->next = new MemFileDataBlock;
		}

		block->next->last = block;
		block = block->next;
		blockOffset += MemFileDataBlock::BlockDataSize;

		writeSize = MemFileDataBlock::BlockDataSize;
		memcpy(block->data, buffer, writeSize); // 完整块
		localOffset = writeSize;

		buffer += writeSize;
		size -= writeSize;
	}

	if (size > 0)
	{
		// 写入末尾

		if (block->next == nullptr)
		{
#if MemDebug && MemUsageDebug
			memTotolUsage += sizeof(MemFileDataBlock);
			printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif
			block->next = new MemFileDataBlock;
		}

		block->next->last = block;
		block = block->next;
		blockOffset += MemFileDataBlock::BlockDataSize;

		writeSize = size;
		memcpy(block->data, buffer, writeSize);
		localOffset = writeSize;
	}

	this->file->size = max(this->file->size, blockOffset + localOffset);
	return sizeParamRecord;
}

size_t MemFileView::read(char* buffer, size_t size)
{
	auto sizeParamRecord = size; // 仅仅是记录

	// 应用偏移
	this->applyLocalOffset();

	//         v blockOffset     v BlockDataSize
	// ... --- |DataData         | --- ...
	//           ^     ^ finalBlockLocalSize
	//           | localOffset

	const size_t finalBlockLocalSize = this->file->size % MemFileDataBlock::BlockDataSize;

	size_t readSize = 0;

	// 读取起始块
	if (block == nullptr) return 0;
	if (block->next != nullptr)
		readSize = min(MemFileDataBlock::BlockDataSize - (size_t)localOffset, size); // 普通块
	else readSize = min(finalBlockLocalSize - (size_t)localOffset, size); // 末尾

	memcpy(buffer, block->data + localOffset, readSize);
	localOffset += readSize;

	buffer += readSize;
	size -= readSize;

	// 读取中间完整块
	while (size > 0 && block->next != nullptr)
	{
		block = block->next;
		blockOffset += MemFileDataBlock::BlockDataSize;

		readSize = min(MemFileDataBlock::BlockDataSize, size);
		memcpy(buffer, block->data, readSize);
		localOffset = readSize;

		buffer += readSize;
		size -= readSize;
	}

	// 读取末尾
	if (size > 0 && block->next == nullptr)
	{
		readSize = min(finalBlockLocalSize - (size_t)localOffset, size);
		memcpy(buffer, block->data, readSize);
		localOffset += readSize;

		buffer += readSize;
		size -= readSize;
	}

	return sizeParamRecord - size;
}

void MemFileView::applyLocalOffset()
{
	// 前移
	while (localOffset < 0)
	{
		if (block->last != nullptr)
		{
			block = block->last;
			blockOffset -= MemFileDataBlock::BlockDataSize;
			localOffset += MemFileDataBlock::BlockDataSize;
		}
		else localOffset = blockOffset = 0;
	}

	// 后移
	while (localOffset > MemFileDataBlock::BlockDataSize)
	{
		if (block->next != nullptr)
		{
			block = block->next;
			blockOffset += MemFileDataBlock::BlockDataSize;
			localOffset -= MemFileDataBlock::BlockDataSize;
		}
		else
		{
			blockOffset = this->file->size / MemFileDataBlock::BlockDataSize;
			localOffset = this->file->size % MemFileDataBlock::BlockDataSize;
		}
	}
}

MemFloorHead::MemFloorHead()
{
	dirent.d_type = (unsigned char)FileType::Floor;
	dirent.d_name[0] = '\0';
}

MemFloorHead::~MemFloorHead()
{
	this->parent = nullptr;
	MemFileHead* file = this->file;
	if (file != nullptr)
	{
		while (file->next != nullptr)
		{
			file = (MemFileHead*)file->next;

#if MemDebug && MemUsageDebug
			memTotolUsage -= sizeof(MemFileHead);
			printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

			delete file->last;
		}

#if MemDebug && MemUsageDebug
		memTotolUsage -= sizeof(MemFileHead);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		delete file;
	}

	MemFloorHead* floor = this->child;
	if (floor != nullptr)
	{
		while (floor->next != nullptr)
		{
			floor = (MemFloorHead*)floor->next;

#if MemDebug && MemUsageDebug
			memTotolUsage -= sizeof(MemFloorHead);
			printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

			delete floor->last;
		}
#if MemDebug && MemUsageDebug
		memTotolUsage -= sizeof(MemFloorHead);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		delete floor;
	}
}

MemFloorHead* MemFloorHead::findChildFloor(const char* floorName, size_t floorNameLenght)
{
	MemFloorHead* floor = child;
	while (floor != nullptr)
	{
		if (stringCompare(floor->name, floor->nameLenght, floorName, floorNameLenght)) break;
		floor = (MemFloorHead*)floor->next;
	}
	return floor;
}

MemFloorHead* MemFloorHead::findFloor(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return this; // pathLenght == 1 -> '/'
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return nullptr; //路径无效
	}

	if (pathLenght == (size_t)-1) // '/'
	{
		printf("%s:%d I just want to figure out whether it matter, now it seem count\n", __FILE__, __LINE__);
		return this;
	}

	MemFloorHead* floor = this;
	size_t begin = 0;
	size_t end = 0;

	// root/path/floor
	// ^              ^
	// |              |
	//begin        Lenght
	while (begin < pathLenght)
	{
		// 找end
		while (path[end] != '/' && end < pathLenght) end++;
		// root/path/floor
		// ^   ^          ^
		// |   |          |
		//begin end    Lenght

		floor = floor->findChildFloor(path + begin, end - begin);
		if (floor == nullptr) return nullptr;

		if (end == pathLenght)
		{
			// root/path/floor
			//      ^    ^    ^
			//      |    |    |
			//   floor begin end
			return floor;
		}

		end++;
		begin = end;
		// root/path/floor
		//      ^         ^
		//      |         |
		//    begin    Lenght
	}

	printf("???How I get Here???\n\tpath=%s\n\tpathLenght=%u\n", path, pathLenght);
	//throw "???How I get Here???"
	return nullptr;
}

MemFileHead* MemFloorHead::findChildFile(const char* fileName, size_t fileNameLenght)
{
	MemFileHead* file = this->file;

	while (file != nullptr)
	{
		if (stringCompare(file->name, file->nameLenght, fileName, fileNameLenght)) break;
		file = (MemFileHead*)file->next;
	}
	return file;
}

MemFileHead* MemFloorHead::findFile(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return nullptr; //路径无效
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return nullptr; //路径无效
	}

	constexpr size_t negtive = (size_t)-1;
	size_t divide = pathLenght - 1;
	MemFloorHead* floor = &memRoot;

	// root/path/file
	//              ^
	while (divide != negtive)
	{
		if (path[divide] == '/')
		{
			// root/path/file
			//          ^
			floor = floor->findFloor(path, divide);
			if (floor == nullptr) return nullptr; //路径无效
			break;
		}
		divide--;
	}

	divide++;
	// root/path/file
	//           ^
	return floor->findChildFile(path + divide, pathLenght - divide);
}

bool MemFloorHead::removeChildFloor(const char* floorName, size_t floorNameLenght)
{
	MemFloorHead* floor = findChildFloor(floorName, floorNameLenght);
	if (floor == nullptr) return false;
	if (floor->child != nullptr || floor->file != nullptr) return false; //非空目录
	if (floor == this->child) this->child = (MemFloorHead*)floor->next;
	if (floor->last != nullptr) floor->last->next = floor->next;
	if (floor->next != nullptr) floor->next->last = floor->last;

#if MemDebug && MemUsageDebug
	memTotolUsage -= sizeof(MemFloorHead);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

	delete floor;
	return true;
}

bool MemFloorHead::removeFloor(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}

	constexpr size_t negtive = (size_t)-1;
	size_t divide = pathLenght - 1;
	MemFloorHead* floor = &memRoot;

	// root/path/floor
	//               ^
	while (divide != negtive)
	{
		if (path[divide] == '/')
		{
			// root/path/floor
			//          ^
			floor = floor->findFloor(path, divide);
			if (floor == nullptr) return false; //路径无效
			break;
		}
		divide--;
	}

	divide++;
	// root/path/floor
	//           ^
	return floor->removeChildFloor(path + divide, pathLenght - divide);
}

bool MemFloorHead::removeChildFile(const char* fileName, size_t fileNameLenght)
{
	MemFileHead* file = findChildFile(fileName, fileNameLenght);
	if (file == nullptr) return false;
	if (file == this->file) this->file = (MemFileHead*)file->next;
	if (file->last != nullptr) file->last->next = file->next;
	if (file->next != nullptr) file->next->last = file->last;

#if MemDebug && MemUsageDebug
	memTotolUsage -= sizeof(MemFileHead);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

	delete file;
	return true;
}

bool MemFloorHead::removeFile(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}

	constexpr size_t negtive = (size_t)-1;
	size_t divide = pathLenght - 1;
	MemFloorHead* floor = &memRoot;

	// root/path/floor
	//               ^
	while (divide != negtive)
	{
		if (path[divide] == '/')
		{
			// root/path/floor
			//          ^
			floor = floor->findFloor(path, divide);
			if (floor == nullptr) return false; //路径无效
			break;
		}
		divide--;
	}

	divide++;
	// root/path/floor
	//           ^
	return floor->removeChildFile(path + divide, pathLenght - divide);
}
bool MemFloorHead::makeChildFloor(const char* floorName, size_t floorNameLenght)
{
	if (this->child == nullptr)
	{

#if MemDebug && MemUsageDebug
		memTotolUsage += sizeof(MemFloorHead);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		this->child = new MemFloorHead;
		this->child->parent = this;
		this->child->nameLenght = floorNameLenght;
		memcpy(this->child->name, floorName, floorNameLenght);
		this->child->name[floorNameLenght] = '\0';
		return true;
	}

	// 查重
	MemFloorHead* floor = this->child;
	while (floor->next != nullptr)
	{
		if (stringCompare(floor->name, floor->nameLenght, floorName, floorNameLenght)) return false; // 已经存在
		floor = (MemFloorHead*)floor->next;
	}
	if (stringCompare(floor->name, floor->nameLenght, floorName, floorNameLenght)) return false; // 已经存在

	// floor -> new floor
	// ^
#if MemDebug && MemUsageDebug
	memTotolUsage += sizeof(MemFloorHead);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif
	floor->next = new MemFloorHead;
	floor->next->last = floor;
	floor = (MemFloorHead*)floor->next;

	// floor -> new floor
	//          ^
	floor->parent = this;
	floor->nameLenght = floorNameLenght;
	memcpy(floor->name, floorName, floorNameLenght);
	floor->name[floorNameLenght] = '\0';
	return true;
}

bool MemFloorHead::makeFloor(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}

	constexpr size_t negtive = (size_t)-1;
	size_t divide = pathLenght - 1;
	MemFloorHead* floor = &memRoot;

	// root/path/floor
	//               ^
	while (divide != negtive)
	{
		if (path[divide] == '/')
		{
			// root/path/floor
			//          ^
			floor = floor->findFloor(path, divide);
			if (floor == nullptr) return false; //路径无效
			break;
		}
		divide--;
	}

	divide++;
	// root/path/floor
	//           ^
	return floor->makeChildFloor(path + divide, pathLenght - divide);
}

bool MemFloorHead::makeChildFile(const char* fileName, size_t fileNameLenght)
{
	if (this->file == nullptr)
	{

#if MemDebug && MemUsageDebug
		memTotolUsage += sizeof(MemFileHead);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		this->file = new MemFileHead;
		MemFileHead& file = *this->file;
		file.parent = this;
		file.nameLenght = fileNameLenght;
		memcpy(file.name, fileName, fileNameLenght);
		file.name[fileNameLenght] = '\0';
		return true;
	}

	// 查重
	MemFileHead* file = this->file;
	while (file->next != nullptr)
	{
		if (stringCompare(file->name, file->nameLenght, fileName, fileNameLenght)) return false; // 已经存在
		file = (MemFileHead*)file->next;
	}
	if (stringCompare(file->name, file->nameLenght, fileName, fileNameLenght)) return false; // 已经存在

	// file -> new file
	// ^
#if MemDebug && MemUsageDebug
	memTotolUsage += sizeof(MemFileHead);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif
	file->next = new MemFileHead;
	file->next->last = file;
	file = (MemFileHead*)file->next;

	// file -> new file
	//         ^

	file->parent = this;
	file->nameLenght = fileNameLenght;
	memcpy(file->name, fileName, fileNameLenght);
	file->name[fileNameLenght] = '\0';
	return true;
}

bool MemFloorHead::makeFile(const char* path, size_t pathLenght)
{
	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return false; //路径无效
	}

	constexpr size_t negtive = (size_t)-1;
	size_t divide = pathLenght - 1;
	MemFloorHead* floor = &memRoot;

	// root/path/floor
	//               ^
	while (divide != negtive)
	{
		if (path[divide] == '/')
		{
			// root/path/floor
			//          ^
			floor = floor->findFloor(path, divide);
			if (floor == nullptr) return false; //路径无效
			break;
		}
		divide--;
	}

	divide++;
	// root/path/floor
	//           ^
	return floor->makeChildFile(path + divide, pathLenght - divide);
}

// vfs file函数

bool mountMem()
{
#if MemDebug && MemProcessDebug
	printf("mount Mem\n");
#endif
	esp_vfs_t memFs;

	memFs.flags = ESP_VFS_FLAG_DEFAULT;
	memFs.open = memOpen;
	memFs.write = memWrite;
	memFs.read = memRead;
	memFs.fstat = memFstat;
	memFs.stat = memStat;
	memFs.close = memClose;
	memFs.lseek = memSeek;
	memFs.rename = memRename;
	memFs.unlink = memUnlink;
	memFs.mkdir = memMakeDir;
	memFs.rmdir = memRemoveDir;
	memFs.opendir = memOpenDir;
	memFs.readdir = memReadDir;
	memFs.telldir = memTellDir;
	memFs.seekdir = memSeekDir;
	memFs.closedir = memCloseDir;

	if (!testFloor(PerfixMem))
	{
		ESP_LOGW("mem", "%s is not exsit, can't mount mem", PerfixMem);
		return false;
	}
	ESP_ERROR_CHECK(esp_vfs_register(PerfixMem, &memFs, NULL));

	return true;
}

int memOpen(const char* path, int flags, int mode)
{
#if MemDebug && MemProcessDebug
	printf("memOpen\n");
#endif
	bool creatFile = flags & O_CREAT;

	size_t pathLenght = strlen(path);
	MemFileHead* file = memRoot.findFile(path, pathLenght);
	if (file == nullptr)
	{
		if (!creatFile) return MemFileSystemError::NotExistFile;
		if (!memRoot.makeFile(path, pathLenght))
			return MemFileSystemError::Error;
		file = memRoot.findFile(path, pathLenght);
	}

	// 找fd
	int fd = 0;
	Lock lock{ mutex };
	while (memFileDescriptions[fd].file != nullptr && fd < MaxMemFileDescriptionCount) fd++;
	// 没有文件描述符
	if (fd == MaxMemFileDescriptionCount) return MemFileSystemError::NoAviliableDescription;

#if MemDebug && MemProcessDebug
	printf("memOpend with fd = %d\n", fd);
#endif
	memFileDescriptions[fd].file = file;
	memFileDescriptions[fd].block = file->dataBlock;
	memFileDescriptions[fd].blockOffset = 0;
	memFileDescriptions[fd].localOffset = 0;
	return fd;
}

int memFstat(int fd, struct stat* st)
{
#if MemDebug && MemProcessDebug
	printf("memFstat\n");
#endif
	if (memFileDescriptions[fd].file == nullptr) return MemFileSystemError::NotOpenedFileDescription;
	*st = memFileDescriptions[fd].file->st;
	return 0;
}

int memStat(const char* path, struct stat* st)
{
	auto pathLenght = strlen(path);

	if (path[pathLenght - 1] == '/')
	{
		pathLenght -= 1;
		if (pathLenght == 0) return MemFileSystemError::UnReachablePath; //路径无效
	}
	if (path[0] == '/')
	{
		path++;
		pathLenght -= 1;
		if (pathLenght == 0) return MemFileSystemError::UnReachablePath; //路径无效
	}

	constexpr size_t negtive = (size_t)-1;
	size_t divide = pathLenght - 1;
	MemFloorHead* floor = &memRoot;

	// root/path/file
	//              ^
	while (divide != negtive)
	{
		if (path[divide] == '/')
		{
			// root/path/file
			//          ^
			floor = floor->findFloor(path, divide);
			if (floor == nullptr) return MemFileSystemError::UnReachablePath; //路径无效
			break;
		}
		divide--;
	}

	divide++;
	// root/path/file
	//           ^


	auto* file = floor->findChildFile(path + divide, pathLenght - divide);
	if (file != nullptr)
	{
		// file
		*st = file->st;
		return 0;
	}
	else if (floor->findChildFloor(path + divide, pathLenght - divide) != nullptr)
	{
		// floor
		*st = {};
		st->st_mode = _IFDIR;
		st->st_size = 0;
		return 0;
	}
	return MemFileSystemError::NotExistFile;
}

ssize_t memWrite(int fd, const void* data, size_t size)
{
#if MemDebug && MemProcessDebug
	printf("memWrite\n");
#endif
	if (memFileDescriptions[fd].file == nullptr) return MemFileSystemError::NotOpenedFileDescription;
	return (memFileDescriptions[fd].write((const char*)data, size));
}

ssize_t memRead(int fd, void* dst, size_t size)
{
#if MemDebug && MemProcessDebug
	printf("memRead\n");
#endif
	if (memFileDescriptions[fd].file == nullptr) return MemFileSystemError::NotOpenedFileDescription;
	return (memFileDescriptions[fd].read((char*)dst, size));
}

int memClose(int fd)
{
#if MemDebug && MemProcessDebug
	printf("memClose\n");
#endif
	if (memFileDescriptions[fd].file == nullptr) return MemFileSystemError::NotOpenedFileDescription;
	memFileDescriptions[fd].file = nullptr;
	return 0;
}

off_t memSeek(int fd, off_t size, int mod)
{
#if MemDebug && MemProcessDebug
	printf("memSeek\n");
#endif
	if (memFileDescriptions[fd].file == nullptr) return MemFileSystemError::NotOpenedFileDescription;

	auto& file = memFileDescriptions[fd];
	switch (mod)
	{
	case SEEK_SET:
		// beg
		file.localOffset += size - (file.blockOffset + file.localOffset);
		break;
	default:
	case SEEK_CUR:
		// cur
		file.localOffset += size;
		break;
	case SEEK_END:
		// end
		file.localOffset += size - (file.blockOffset + file.localOffset) + file.file->size;
		break;
	}

	return file.blockOffset + file.localOffset;
}

int memRename(const char* src, const char* dst)
{
#if MemDebug && MemProcessDebug
	printf("memRename\n");
#endif
	return MemFileSystemError::UnSupportOpreation;
}

int memUnlink(const char* path)
{
#if MemDebug && MemProcessDebug
	printf("memUnlink\n");
#endif
	size_t pathLenght = strlen(path);
	if (path[pathLenght - 1] == '/')
		return memRoot.removeFloor(path, pathLenght) ? 0 : MemFileSystemError::Error;
	else
		return memRoot.removeFile(path, pathLenght) ? 0 : MemFileSystemError::Error;
}

// vfs floor函数

DIR* memOpenDir(const char* name)
{
#if MemDebug && MemProcessDebug
	printf("memOpenDir\n");
#endif

#if MemDebug && MemUsageDebug
	memTotolUsage += sizeof(MemFloorDir);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

	auto ret = new MemFloorDir;
	ret->floor = memRoot.findFloor(name, strlen(name));
	if (ret->floor == nullptr)
	{

#if MemDebug && MemUsageDebug
		memTotolUsage -= sizeof(MemFloorDir);
		printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

		delete ret;
		return nullptr;
	}

	ret->operatorChild = ret->floor->child;
	ret->operatorFile = ret->floor->file;
	return (DIR*)ret;
}

dirent* memReadDir(DIR* pdir)
{
#if MemDebug && MemProcessDebug
	printf("memReadDir\n");
#endif
	MemFloorDir& dir = *(MemFloorDir*)pdir;
	MemFloorHead*& floor = dir.floor;
	MemFloorHead*& child = dir.operatorChild;
	MemFileHead*& file = dir.operatorFile;

	if (child != nullptr && child->parent == floor) // 防止child被delete
	{
		dirent* ret = &child->dirent;
		child = (MemFloorHead*)child->next;
		dir.operatorCount++;
		return ret;
	}

	if (file != nullptr && file->parent == floor) // 防止file被delete
	{
		dirent* ret = &file->dirent;
		file = (MemFileHead*)file->next;
		dir.operatorCount++;
		return ret;
	}
	return nullptr;
}

long memTellDir(DIR* pdir)
{
#if MemDebug && MemProcessDebug
	printf("memTellDir\n");
#endif
	MemFloorDir& dir = *(MemFloorDir*)pdir;
	return dir.operatorCount;
}

void memSeekDir(DIR* pdir, long offset)
{
#if MemDebug && MemProcessDebug
	printf("memSeekDir\n");
#endif
	MemFloorDir& dir = *(MemFloorDir*)pdir;
	MemFloorHead*& child = dir.operatorChild;
	MemFileHead*& file = dir.operatorFile;
	child = dir.floor->child;
	file = dir.floor->file;
	for (size_t i = 0;i < offset;i++)
		memReadDir(pdir);
}

int memCloseDir(DIR* pdir)
{
#if MemDebug && MemProcessDebug
	printf("memCloseDir\n");
#endif

#if MemDebug && MemUsageDebug
	memTotolUsage -= sizeof(MemFloorDir);
	printf("mem: totol memory usage = %u at %d\n", memTotolUsage, __LINE__);
#endif

	delete (MemFloorDir*)pdir;
	return 0;
}

int memMakeDir(const char* name, mode_t mode)
{
#if MemDebug && MemProcessDebug
	printf("memMakeDir\n");
#endif
	return memRoot.makeFloor(name, strlen(name));
}

int memRemoveDir(const char* name)
{
#if MemDebug && MemProcessDebug
	printf("memRemoveDir\n");
#endif
	if (memRoot.removeFloor(name, strlen(name))) return 0;
	else return MemFileSystemError::Error;
}
