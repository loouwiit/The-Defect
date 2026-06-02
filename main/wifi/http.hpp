#pragma once
#include "wifi/socketStream.hpp"
#include "utility/nonCopyAble.hpp"
#include "storage/fat.hpp"

using HttpStringPairSize_t = uint8_t;
using HttpFileBodyType = IFile;

constexpr const char HttpVersion[] = "HTTP/1.1";
constexpr const size_t HttpVersionSize = sizeof(HttpVersion) - 1;
constexpr const size_t HttpUrlBufferLenght = 256;
constexpr const size_t HttpReasonBufferLenght = 64;

constexpr const size_t HttpStringPairsAddSpeed = 2;

constexpr const size_t HttpHeadsBufferLenght = 512;

constexpr const size_t HttpBodySendingBufferLenght = 1024;

enum class HttpMethod
{
	Null,
	Get,
	Post,
	Put,
	Delete,
};

namespace HttpMethodName
{
	constexpr const char Null[] = "";
	constexpr const char Get[] = "GET";
	constexpr const char Post[] = "POST";
	constexpr const char Put[] = "PUT";
	constexpr const char Delete[] = "DELETE";
}

enum class HttpStatus
{
	Null = 0,
	Ok = 200,
	BadRequest = 400,
	Unauthorized = 401,
	Forbidden = 403,
	NotFound = 404,
	InternalServerError = 500
};

namespace HttpReason
{
	constexpr const char Null[] = "";
	constexpr const char Ok[] = "OK";
	constexpr const char BadRequest[] = "Bad Request";
	constexpr const char Unauthorized[] = "Unauthorized";
	constexpr const char Forbidden[] = "Forbidden";
	constexpr const char NotFound[] = "Not Found";
	constexpr const char InternalServerError[] = "Internal Server Error";
}

namespace HttpHeadName
{
	constexpr const char ContentLenght[] = "Content-Length";
	constexpr const char ContentType[] = "Content-Type";

	constexpr const char Cookie[] = "Cookie";
	constexpr const char SetCookie[] = "Set-Cookie";

	constexpr const char MaxAge[] = "Max-Age";
	constexpr const char Path[] = "Path";
}

enum class HttpContentType
{
	Html,
	Javascript,
	Css,

	Other
};

namespace HttpContentTypeName
{
	constexpr char Html[] = "text/html; charset=utf-8";
	constexpr char Javascript[] = "application/x-javascript; charset=utf-8";
	constexpr char Css[] = "text/css; charset=utf-8";

	constexpr char Other[] = "application/octet-stream; charset=utf-8";
}

//functions

size_t stoa(size_t size, char* buffer, size_t bufferSize);
size_t atos(char* buffer);
size_t atos(char* buffer, size_t bufferSize);

// httpStringPair

class HttpStringPair
{
public:
	HttpStringPair() = default;
	HttpStringPair(const char* name, const char* value);
	HttpStringPair(const HttpStringPair&);
	HttpStringPair(HttpStringPair&& stringPair);
	~HttpStringPair();

	HttpStringPair& operator=(const HttpStringPair& stringPair);
	HttpStringPair& operator=(HttpStringPair&& stringPair);

	void swap(HttpStringPair&);
	void copy(const HttpStringPair&);

	void clear();

	void setName(const char* name);
	void setValue(const char* value);

	const char* getName();
	const char* getValue();

private:
	char* name = nullptr;
	char* value = nullptr;
};

// httpPairs

template <class T>
class HttpPairs : virtual public NonCopyAble
{
public:
	~HttpPairs();

	void setNumber(HttpStringPairSize_t number);
	HttpStringPairSize_t getNumber();

	void clear();

	void add(const T& pair);
	void add(T&& pair);

	bool set(HttpStringPairSize_t index, const T& stringPair);

	T& get(HttpStringPairSize_t index);
	T& operator[](HttpStringPairSize_t index);

private:
	HttpStringPairSize_t stringPairsLenght = 0;
	HttpStringPairSize_t stringPairsPointer = 0;
	T* stringPairs = nullptr;
};

//HttpCookie
class HttpCookie : virtual public HttpStringPair
{
public:
	HttpCookie() = default;
	HttpCookie(const char* name, const char* value, uint32_t maxAge = 0, const char* path = nullptr);
	HttpCookie(const HttpCookie& copy);
	HttpCookie(HttpCookie&& move);
	~HttpCookie();

	HttpCookie& operator=(const HttpCookie& copy);

	void clear();

	void setMaxAge(uint32_t age);
	uint32_t getMaxAge();

	void setPath(const char* path);
	const char* getPath();

private:
	uint32_t maxAge = 0;
	char* path = nullptr;
};

// httpCookies

class HttpCookies : virtual public HttpPairs<HttpCookie>
{
public:
	HttpStringPairSize_t receive(char* buffer);
	void send(OSocketStream& socketStream, bool cookieSet);
};

// httpHeads

class HttpHeads : virtual public HttpPairs<HttpStringPair>
{
public:
	HttpCookies cookies;
	HttpCookies& getCookies();

	HttpStringPairSize_t receive(ISocketStream& socketStream);
	void send(OSocketStream& socketStream, bool cookieSet);

	size_t contentLenght = 0;
};

// httpBase

class HttpBase
{
public:
	HttpHeads heads;
	HttpHeads& getHeads();

	HttpCookies& cookies = heads.cookies;
	HttpCookies& getCookies();

	size_t& bodyLenght = heads.contentLenght;
	size_t getBodyLenght();
	void setBodyLenght(size_t lenght);

	bool isBodyRecieved();
	bool isBodyFile();
	void* getBody();
	void setBody(HttpFileBodyType& file);
	void setBody(void* body);

protected:
	bool bodyIsFile = false;
	bool bodyIsRecieved = false;
	void* body = nullptr;
};

// request

class HttpRequest : virtual public HttpBase, virtual public NonCopyAble
{
public:
	~HttpRequest();

	void receive(ISocketStream& socketStream);
	void send(OSocketStream& socketStream);

	void setMethod(HttpMethod method);
	HttpMethod getMethod();

	void setPath(const char* path);
	const char* getPath();

private:
	HttpMethod method = HttpMethod::Get;
	char* path = nullptr;

	static size_t decodePercentUrl(char* url);
	static char decodePercent(char A, char B);
};

// respond

class HttpRespond : virtual public HttpBase
{
public:
	~HttpRespond();

	void receive(ISocketStream& socketStream);
	void send(OSocketStream& socketStream);

	void setStatus(HttpStatus status);
	HttpStatus getStatus();

	void setReason(const char* reason);
	const char* getReason();

private:
	HttpStatus status = HttpStatus::Ok;
	char* reason = nullptr;
};

//function

const char* getReasonFromStatus(const HttpStatus status);
const char* getMethodNameFromMethod(const HttpMethod method);
HttpMethod getMethodFromName(const char* name);
HttpContentType getContentTypeFromPath(const char* path, size_t length);
HttpContentType getContentTypeFromPath(const char* path);
const char* getContentTypeNameFromContentType(HttpContentType type);
