#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/types.h>
#include <fcntl.h>

#include <cstring> //strlen()
#include "esp_log.h"
#include "fat.hpp"

static constexpr char TAG[] = "fat";

static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

bool mountFlash()
{
	ESP_LOGI(TAG, "mounting flash");
	esp_vfs_fat_mount_config_t mount_config{};

	mount_config.format_if_mount_failed = false;
	mount_config.max_files = FlashMaxFileNumber;
	mount_config.allocation_unit_size = CONFIG_WL_SECTOR_SIZE;
	mount_config.disk_status_check_enable = false;

	esp_err_t err;
	err = esp_vfs_fat_spiflash_mount_rw_wl(PerfixFlash, FlashPartitionLabel, &mount_config, &s_wl_handle);

	if (err == ESP_OK)
	{
		ESP_LOGI(TAG, "flash mount success");
		return true;
	}
	else
	{
		ESP_LOGE(TAG, "Failed to mount flash (%s)", esp_err_to_name(err));
		return false;
	}
}

void unmountFlash()
{
	ESP_LOGI(TAG, "unmounting flash");
	ESP_ERROR_CHECK(esp_vfs_fat_spiflash_unmount_rw_wl(PerfixFlash, s_wl_handle));
	ESP_LOGI(TAG, "flash unmounted");
}

void formatFlash()
{
	ESP_LOGW(TAG, "formating flash");
	esp_err_t ret = esp_vfs_fat_spiflash_format_rw_wl(PerfixFlash, FlashPartitionLabel);
	if (ret == ESP_OK)
	{
		ESP_LOGI(TAG, "format success");
	}
	else
	{
		ESP_LOGE(TAG, "format failed: %s", esp_err_to_name(ret));
	}
}

void getSpace(uint64_t& free, uint64_t& totol, const char* path)
{
	esp_vfs_fat_info(path, &totol, &free);
}

uint64_t getFreeSpace(const char* path)
{
	uint64_t bytes_total = 0;
	uint64_t bytes_free = 0;
	esp_vfs_fat_info(path, &bytes_total, &bytes_free);
	return bytes_free;
}

uint64_t getTotolSpace(const char* path)
{
	uint64_t bytes_total = 0;
	uint64_t bytes_free = 0;
	esp_vfs_fat_info(path, &bytes_total, &bytes_free);
	return bytes_total;
}

bool tree(const char* path, unsigned char maxOffset, unsigned char offset)
{
	if (offset >= maxOffset)
	{
		ESP_LOGW(TAG, "tree offset %d, which is touched the max, in %s", offset, path);
		return true;
	}

	if (offset == 0)
	{
		ESP_LOGI(TAG, "tree at %s:", path);
	}
	//printf("[debug]tree at %s:\n", path);

	Floor floor;
	if (!floor.open(path))
	{
		putchar('|');
		for (unsigned char i = 0; i < offset;i++)
		{
			putchar('|');
		}
		ESP_LOGW(TAG, "-tree: fail in open %s", path);
		// printf("-tree: formating flash\n");
		// unmountFlash();
		// formatFlash();
		return false;
	}

	const char* sub = nullptr;
	size_t subLenght = 0;
	char* buffer = nullptr;
	bool fine = true;

	while (true)
	{
		sub = floor.read(Floor::Type::Floor);
		if (sub == nullptr) break;

		subLenght = strlen(sub);
		putchar('|');
		for (unsigned char i = 0; i < offset;i++)
		{
			putchar('|');
		}
		ESP_LOGI(TAG, "-dir: %s", sub);

		buffer = new char[subLenght + floor.getPathLenght() + 2];memcpy(buffer, floor.getPath(), floor.getPathLenght());
		buffer[floor.getPathLenght()] = '/';
		memcpy(buffer + floor.getPathLenght() + 1, sub, subLenght + 1);
		fine = tree(buffer, maxOffset, offset + 1);
		delete[] buffer;

		if (fine) continue;
		else return false;
	}

	floor.backToBegin();
	while (true)
	{
		sub = floor.read(Floor::Type::File);
		if (sub == nullptr) break;

		putchar('|');
		for (unsigned char i = 0; i < offset;i++)
		{
			putchar('|');
		}
		ESP_LOGI(TAG, "-file: %s", sub);
	}

	return true;
}

bool newFile(const char* path)
{
	if (testFile(path))
		return false;

	OFile file{};
	if (file.open(path)) return true;
	else return false;
}

bool testFile(const char* path)
{
	IFile file{};
	if (file.open(path)) return true;
	else return false;
}

bool moveFile(const char* oldPath, const char* newPath)
{
	return rename(oldPath, newPath) == 0;
}

bool removeFile(const char* path)
{
	return unlink(path) == 0;
}

bool newFloor(const char* path)
{
	return mkdir(path, 0777) == 0;
}

bool testFloor(const char* path)
{
	Floor floor;
	if (floor.open(path))
	{
		floor.close();
		return true;
	}
	{
		return false;
	}
}

bool removeFloor(const char* path)
{
	return rmdir(path) == 0;
}

bool removeFloorRecursive(const char* path)
{
	// 非目录 → 按文件删
	if (!testFloor(path))
	{
		if (testFile(path)) return removeFile(path);
		return false;
	}

	Floor floor;
	if (!floor.open(path)) return false;

	// 递归删除子目录
	const char* sub = nullptr;
	size_t subLenght = 0;
	char* buffer = nullptr;
	while (true)
	{
		sub = floor.read(Floor::Type::Floor);
		if (sub == nullptr) break;
		// 保险：跳过 "." / ".."（FatFs 一般不返回，防御死循环）
		if (strcmp(sub, ".") == 0 || strcmp(sub, "..") == 0) continue;

		subLenght = strlen(sub);
		buffer = new char[subLenght + floor.getPathLenght() + 2];
		memcpy(buffer, floor.getPath(), floor.getPathLenght());
		buffer[floor.getPathLenght()] = '/';
		memcpy(buffer + floor.getPathLenght() + 1, sub, subLenght + 1);
		bool ok = removeFloorRecursive(buffer);
		delete[] buffer;
		if (!ok) return false;
	}

	// 删除文件
	floor.backToBegin();
	while (true)
	{
		sub = floor.read(Floor::Type::File);
		if (sub == nullptr) break;

		subLenght = strlen(sub);
		buffer = new char[subLenght + floor.getPathLenght() + 2];
		memcpy(buffer, floor.getPath(), floor.getPathLenght());
		buffer[floor.getPathLenght()] = '/';
		memcpy(buffer + floor.getPathLenght() + 1, sub, subLenght + 1);
		removeFile(buffer);
		delete[] buffer;
	}
	floor.close();

	// 最后删除空目录本身
	return removeFloor(path);
}

const char* getBaseName(const char* path)
{
	auto ret = path;
	while (*ret != '\0') ret++;
	while (ret != path && *ret != '/' && *ret != '\\') ret--;
	if (*ret == '/' || *ret == '\\') ret++;
	return ret;
}

//FileBase

FileBase::~FileBase()
{
	this->close();
}

void FileBase::close()
{
	if (file != NoneFile)
	{
		::close(file);
		file = NoneFile;
	}
}

bool FileBase::isOpen()
{
	return file != NoneFile;
}

FileBase::OffsetType FileBase::getOffset()
{
	return lseek(file, 0, (int)OffsetMode::Current);
}

void FileBase::setOffset(FileBase::OffsetType offset, OffsetMode mode)
{
	lseek(file, offset, (int)mode);
}

void FileBase::offset(FileBase::OffsetType offset)
{
	lseek(file, offset, (int)OffsetMode::Current);
}

void FileBase::reGetSize()
{
	OffsetType origin = getOffset();
	setOffset(0, OffsetMode::End);
	fileSize = getOffset();
	setOffset(origin);
}

FileBase::OffsetType FileBase::getSize()
{
	return fileSize;
}

FileBase::operator FileDescription()
{
	return file;
}

//IFile

bool IFile::open(const char* path)
{
	if (file != NoneFile)
		close();
	fileSize = 0;
	file = ::open(path, O_RDONLY);
	return file != NoneFile;
}

char IFile::get()
{
	char buffer = '\0';
	read(&buffer, 1);
	return buffer;
}

size_t IFile::getLine(char* string, size_t size, char ch)
{
	size_t ret = 0;
	while (ret < size)
	{
		string[ret] = get();
		if (string[ret] == ch) break;
		if (eof()) break;
		ret++;
	}
	string[ret] = '\0';
	return ret;
}

size_t IFile::read(void* pointer, size_t size)
{
	return ::read(file, (char*)pointer, size);
}

bool IFile::eof()
{
	return getOffset() < fileSize;
}

//Ofile

bool OFile::open(const char* path)
{
	if (file != NoneFile)
		close();
	fileSize = 0;
	file = ::open(path, O_RDWR | O_CREAT);
	return file != NoneFile;
}

bool OFile::put(const char ch)
{
	return write(&ch, 1) != 0;
}

size_t OFile::write(const void* pointer, size_t size)
{
	return ::write(file, pointer, size);
}

Floor::~Floor()
{
	if (dir != nullptr)
	{
		closedir(dir);
		dir = nullptr;
	}

	if (path != nullptr)
	{
		delete[] path;
		path = nullptr;
	}
}

bool Floor::open(const char* path)
{
	if (this->path != nullptr)
		delete[] this->path;
	pathBufferLenght = strlen(path);
	if (pathBufferLenght == 0) return false; // 空串防御（原 path[-1] 越界读）
	if (path[pathBufferLenght - 1] != '/')
		pathBufferLenght++; // for '/'
	this->path = new char[pathBufferLenght];
	memcpy(this->path, path, pathBufferLenght);
	if (path[pathBufferLenght - 1] == '/')
		this->path[pathBufferLenght - 1] = '\0';

	if (dir != nullptr)
		closedir(dir);
	dir = opendir(path);
	count[0] = 0;
	count[1] = 0;
	return dir != nullptr;
}

void Floor::close()
{
	if (dir != nullptr)
	{
		closedir(dir);
		dir = nullptr;
	}

	if (path != nullptr)
	{
		delete[] path;
		path = nullptr;
	}
}

const char* Floor::getPath()
{
	return path;
}

size_t Floor::getPathLenght()
{
	return pathBufferLenght - 1;
}

bool Floor::openFloor(const char* path, size_t pathLenght)
{
	size_t bufferSize = pathLenght + this->pathBufferLenght + 1;
	char* buffer = new char[bufferSize];
	memcpy(buffer, this->path, this->pathBufferLenght - 1); // \0 不需要复制
	buffer[pathBufferLenght - 1] = '/';
	memcpy(buffer + this->pathBufferLenght, path, pathLenght + 1); // with \0

	if (buffer[bufferSize - 1] == '/')
		buffer[bufferSize - 1] = '\0';

	DIR* temp = opendir(buffer);
	delete[] buffer;
	if (temp != nullptr)
	{
		closedir(dir);
		dir = temp;
		temp = nullptr;
		return true;
	}
	else
	{
		return false;
	}
}

bool Floor::openFile(const char* path, size_t pathLenght, FileBase& file)
{
	char* buffer = new char[pathLenght + this->pathBufferLenght + 1];
	memcpy(buffer, this->path, pathBufferLenght - 1); // \0 不需要复制
	buffer[pathBufferLenght - 1] = '/';
	memcpy(buffer + pathBufferLenght, path, pathLenght + 1); // with \0

	bool ret = file.open(buffer);
	delete[] buffer;
	return ret;
}

const char* Floor::read(Type type)
{
	if (dir == nullptr) return nullptr;

	dirent* content;
	do
	{
		content = readdir(dir);
	} while (content != nullptr && (content->d_type & (TypeBase)type) == 0);
	if (content != nullptr) return content->d_name;
	else return nullptr;
}

void Floor::backToBegin()
{
	if (dir != nullptr)
	{
		rewinddir(dir);
	}
}

void Floor::reCount()
{
	if (dir == nullptr) return;
	backToBegin();
	count[0] = 0;
	count[1] = 0;

	dirent* buffer = nullptr;
	while ((buffer = readdir(dir)) != nullptr)
	{
		switch (buffer->d_type)
		{
		case (TypeBase)Type::File:
			count[0]++;
			break;
		case (TypeBase)Type::Floor:
			count[1]++;
			break;
		default:
			ESP_LOGE(TAG, "Floor::reCount: error in %s 's %s", path, buffer->d_name);
			break;
		}
	}

	backToBegin();
}

size_t Floor::getCount(Type type)
{
	switch (type)
	{
	case Type::File:
		return count[0];
	case Type::Floor:
		return count[1];
	case Type::Both:
		return count[0] + count[1];
	default:
		return 0;
	}
}