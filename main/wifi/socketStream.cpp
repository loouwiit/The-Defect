#include "socketStream.hpp"

#include <algorithm>
#include <errno.h>
#include <lwip/sockets.h>

// SocketStreamBase

Socket SocketStreamBase::getSocket()
{
	return socket;
}

SocketStreamBase::operator Socket()
{
	return socket;
}

bool SocketStreamBase::isGood()
{
	return socketGood;
}

void SocketStreamBase::close()
{
	shutdown(socket, 0);
	::close(socket);
	socketGood = false;
}

// ISocketStream

ISocketStream::ISocketStream(Socket socket)
{
	this->socket = socket;
	if (IBuffer == nullptr)
	{
		IBuffer = new char[SocketStreamBufferSize];
	}
}

ISocketStream::~ISocketStream()
{
	if (IBuffer != nullptr)
	{
		delete[] IBuffer;
		IBuffer = nullptr;
	}
}

void ISocketStream::setSocket(Socket socket)
{
	this->socket = socket;
	IBufferLenght = 0;
	socketGood = true;
}

char ISocketStream::get()
{
	if (!checkIBuffer())
		return 0;

	// IPointer有效
	char ret = IBuffer[IPointer];
	IPointer++;
	return ret;
}

char ISocketStream::peek()
{
	if (!checkIBuffer())
		return 0;

	// IPointer有效
	return IBuffer[IPointer];
}

size_t ISocketStream::readByte(char* buffer, size_t bufferSize)
{
	// 无效指针
	if (buffer == nullptr || bufferSize == 0)
		return 0;

	// buffer无效
	if (!checkIBuffer())
		return 0;

	// 可以读取
	bool reading = true;
	size_t readCount = 0;

	size_t bufferLeave = bufferSize;
	size_t IBufferRead = 0;
	size_t readNumber = 0;

	do
	{
		IBufferRead = IBufferLenght - IPointer;		// 计算IBuffer可用长度
		readNumber = std::min(IBufferRead, bufferLeave); // 计算读取数量

		if (readNumber == 0) // 没有读取的值
		{
			reading = false; // 取消循环
			break;			 // 跳出循环
		}

		memcpy(buffer + readCount, IBuffer + IPointer, readNumber); // 读取

		readCount += readNumber;   // 偏移指针
		IPointer += readNumber;	   // 偏移指针
		bufferLeave -= readNumber; // 计算剩余

		reading = checkIBuffer(); // 判断是否读取完毕
	} while (reading);

	return readCount;
}

size_t ISocketStream::read(char* buffer, size_t bufferSize)
{
	size_t readCount = readByte(buffer, bufferSize - 1);
	buffer[readCount] = '\0'; // 规范字符串
	return readCount;
}

size_t ISocketStream::getline(char* buffer, size_t bufferSize, char end)
{
	if (!checkIBuffer())
		return 0;

	bool reading = true;
	size_t readCount = 0;

	size_t bufferLeave = bufferSize - 1; // 留下一个给\0
	size_t IBufferRead = 0;
	size_t endPointer = 0;

	size_t readNumber = 0;

	do
	{
		// 寻找结尾
		endPointer = IPointer;
		while (endPointer < IBufferLenght && IBuffer[endPointer] != end)
		{
			endPointer++;
		} // 这里好像用for更简单，但是没有循环体还是算了吧

		// 计算读取数量
		IBufferRead = endPointer - IPointer;
		readNumber = std::min(IBufferRead, bufferLeave);

		if (readNumber == 0) // 没有读取的值
		{
			reading = false; // 取消循环
			break;			 // 跳出循环
		}

		memcpy(buffer + readCount, IBuffer + IPointer, readNumber); // 读取

		readCount += readNumber;   // 偏移指针
		IPointer += readNumber;	   // 偏移指针
		bufferLeave -= readNumber; // 计算剩余

		reading = checkIBuffer(); // 判断是否读取完毕
	} while (reading);

	buffer[readCount] = '\0'; // 规范字符串
	if (IBuffer[IPointer] == end)
		IPointer++;

	return readCount;
}

size_t ISocketStream::ignore(char ch, size_t maxNumber)
{
	size_t ignoredNumber = 0;

	while (maxNumber > 0 && checkIBuffer() && IBuffer[IPointer] == ch)
	{
		ignoredNumber++;
		IPointer++;
		maxNumber--;
	}

	return ignoredNumber;
}

size_t ISocketStream::ignoreVoid(size_t maxNumber)
{
	size_t ignoredNumber = 0;

	//[优化]此处checkIBuffer可优化，避免多次检查sood带来的性能下降
	while (maxNumber > 0 && checkIBuffer() && ((IBuffer[IPointer] == ' ') || (IBuffer[IPointer] == '\r') || (IBuffer[IPointer] == '\n')))
	{
		ignoredNumber++;
		IPointer++;
		maxNumber--;
	}

	return ignoredNumber;
}

bool ISocketStream::checkIBuffer()
{
	if (!socketGood)
		return false;

	if (IPointer >= IBufferLenght)
	{
		// 尝试重新读取
		ssize_t ret;
		ret = lwip_recv(socket, IBuffer, SocketStreamBufferSize, MSG_DONTWAIT);
		if (ret == (ssize_t)-1)
		{
			if (errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK)
			{
				// 错误
				socketGood = false;
				IBufferLenght = 0;
				printf("ISocketStream: socket error %d\n", errno);
			}
			return false; // 仍无缓冲区可用
		}
		else if (ret == 0)
		{
			// 结束
			socketGood = false;
			IBufferLenght = 0;
			return false;
		}
		else
		{
			// 正常
			IBufferLenght = (SocketBufferSize_t)ret;
			IPointer = 0;
		}
	}

	return true;
}

// OSocketStream

OSocketStream::OSocketStream(Socket socket)
{
	this->socket = socket;
	if (OBuffer == nullptr)
	{
		OBuffer = new char[SocketStreamBufferSize];
	}
}

OSocketStream::~OSocketStream()
{
	if (OBuffer != nullptr)
	{
		delete[] OBuffer;
		OBuffer = nullptr;
	}
}

void OSocketStream::setSocket(Socket socket)
{
	this->socket = socket;
	OPointer = 0;
	socketGood = true;
}

bool OSocketStream::sendNow()
{
	if (!socketGood)
		return false;

	if (OPointer == 0)
		return true;

	ssize_t ret;
	ret = lwip_send(socket, OBuffer, OPointer, 0);
	if (ret == (ssize_t)-1)
	{
		if (errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			// 错误
			socketGood = false;
			printf("OSocketStream: socket error %d\n", errno);
		}
		return false; // 发送失败，未达成全部发送的目标
	}
	else if (ret == 0)
	{
		// 缓冲区满
		return false;
	}
	else
	{
		// 正常
		SocketBufferSize_t sendLenght = (SocketBufferSize_t)ret;
		SocketBufferSize_t slow = 0;
		SocketBufferSize_t fast = sendLenght;

		while (fast < OPointer)
		{
			// 移动数据
			OBuffer[slow] = OBuffer[fast];
			slow++;
			fast++;
		}

		// 移动指针
		OPointer -= sendLenght;
		if (OPointer == 0)
			return true; // 完全发送
		else
			return false; // 部分发送
	}
}

bool OSocketStream::put(const char ch)
{
	if (!checkOBuffer())
		return false;

	OBuffer[OPointer] = ch;
	OPointer++;
	return true;
}

size_t OSocketStream::write(const char* buffer, size_t bufferSize)
{
	SocketBufferSize_t bufferLeave = SocketStreamBufferSize - OPointer;
	if (bufferSize < bufferLeave)
	{
		// 能放下：放入缓冲区内
		memcpy(OBuffer + OPointer, buffer, bufferSize);
		OPointer += bufferSize;
		return bufferSize;
	}
	else
	{
		// 放不下：分两次发送全部数据

		// 先发送已有缓冲区的数据
		if (!sendNow())
		{
			// 未全部发送，即缓冲区已满
			return 0; // 返回未发送
		}

		ssize_t ret;
		ret = lwip_send(socket, buffer, bufferSize, 0);
		if (ret == (ssize_t)-1)
		{
			// 错误
			socketGood = false;
			printf("OSocketStream: socket error %d\n", errno);
			return 0;
		}
		return ret;
	}
}

bool OSocketStream::checkOBuffer()
{
	if (!socketGood)
		return false;

	// 检查缓冲区
	if (OPointer >= SocketStreamBufferSize)
		sendNow();

	if (!socketGood)
		return false;

	return OPointer < SocketStreamBufferSize;
}

// IOSocketStream
IOSocketStream::IOSocketStream(Socket socket)
	: ISocketStream(socket), OSocketStream(socket)
{}

void IOSocketStream::setSocket(Socket socket)
{
	this->socket = socket;
	ISocketStream::IBufferLenght = 0;
	OSocketStream::OPointer = 0;
	socketGood = true;
}
