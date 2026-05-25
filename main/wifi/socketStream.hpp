#pragma once
#include <lwip/sockets.h>
#include "utility/nonCopyAble.hpp"

using SocketBufferSize_t = uint8_t;
constexpr SocketBufferSize_t SocketStreamBufferSize = 64;

using Socket = int;

class SocketStreamBase
{
public:
	Socket getSocket();
	operator Socket();
	bool isGood();
	void close();

protected:
	Socket socket = 0;
	bool socketGood = false;
};

class ISocketStream : virtual public SocketStreamBase, public NonCopyAble
{
public:
	ISocketStream(Socket socket = 0);
	~ISocketStream();

	void setSocket(Socket socket);

	char get();
	char peek();
	bool check() { return checkIBuffer(); }

	size_t readByte(char* buffer, size_t bufferSize);
	size_t read(char* buffer, size_t bufferSize);
	size_t getline(char* buffer, size_t bufferSize, char end = '\n');

	size_t ignore(char ch, size_t maxNumber = 1);
	size_t ignoreVoid(size_t maxNumber = (size_t)-1);

protected:
	char* IBuffer = nullptr;
	SocketBufferSize_t IBufferLenght = 0;
	SocketBufferSize_t IPointer = 0;

	bool checkIBuffer();
};

class OSocketStream : virtual public SocketStreamBase, public NonCopyAble
{
public:
	OSocketStream(Socket socket = 0);
	~OSocketStream();

	void setSocket(Socket socket);

	bool sendNow();

	bool put(const char ch);

	size_t write(const char* buffer, size_t bufferSize);

protected:
	char* OBuffer = nullptr;
	SocketBufferSize_t OPointer = 0;

	bool checkOBuffer();
};

class IOSocketStream : virtual public ISocketStream, virtual public OSocketStream
{
public:
	IOSocketStream(Socket socket = 0);
	void setSocket(Socket socket);
};
