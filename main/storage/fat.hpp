#pragma once
#include <esp_vfs_fat.h>
#include <dirent.h>
#include "utility/nonCopyAble.hpp"
#include "vfs.hpp"

constexpr char FlashPartitionLabel[] = "fat";
constexpr size_t FlashMaxFileNumber = 16;

bool mountFlash();
void unmountFlash();
void formatFlash();
void getSpace(uint64_t& free, uint64_t& totol, const char* path = PerfixRoot);
uint64_t getFreeSpace(const char* path = PerfixRoot);
uint64_t getTotolSpace(const char* path = PerfixRoot);

bool tree(const char* path, unsigned char maxOffset = 10, unsigned char offset = 0);

bool newFile(const char* path);
bool testFile(const char* path);
bool moveFile(const char* oldPath, const char* newPath);
bool removeFile(const char* path);

bool newFloor(const char* path);
bool testFloor(const char* path);
bool removeFloor(const char* path);

const char* getBaseName(const char* path);

class FileBase : public NonCopyAble
{
public:
	using FileDescription = int;
	using OffsetType = off_t;

	constexpr static FileDescription NoneFile = -1;

	enum class OffsetMode
	{
		Begin = SEEK_SET,
		Current = SEEK_CUR,
		End = SEEK_END,
	};

	~FileBase();

	virtual bool open(const char* path) = 0;
	void close();
	bool isOpen();

	OffsetType getOffset();
	void setOffset(OffsetType offset, OffsetMode mode = OffsetMode::Begin);
	void offset(OffsetType offset);

	void reGetSize();
	OffsetType getSize();

	operator FileDescription ();

protected:
	FileDescription file = NoneFile;
	OffsetType fileSize = 0;
};

class IFile : virtual public FileBase
{
public:
	bool open(const char* path);
	char get();
	size_t getLine(char* string, size_t size, char ch = '\n');
	size_t read(void* pointer, size_t size);

	bool eof();
};

class OFile : virtual public FileBase
{
public:
	bool open(const char* path);
	bool put(const char ch);
	size_t write(const void* pointer, size_t size);
};

class IOFile : virtual public IFile, virtual public OFile
{
public:
	//using OFile::open;
	bool open(const char* path) { return OFile::open(path); }
};

class Floor
{
public:
	using TypeBase = FileTypeBase;
	using Type = FileType;

	~Floor();

	bool open(const char* path);
	void close();

	const char* getPath();
	size_t getPathLenght();

	bool openFloor(const char* path, size_t pathLenght);
	bool openFile(const char* path, size_t pathLenght, FileBase& file);

	const char* read(Type type);

	void backToBegin();
	void reCount();
	size_t getCount(Type type);

private:
	DIR* dir = nullptr;
	size_t count[2] = { 0,0 };

	char* path = nullptr;
	size_t pathBufferLenght = 0;
};