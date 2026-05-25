#include "http.hpp"

bool compareStringCase(const char* a, const char* b);

bool compareString(const char* a, const char* b);

size_t findString(const char* buffer, const char ch, size_t bufferLenght);

bool compareStringCase(const char* a, const char* b)
{
	size_t pointerA = (size_t)-1;
	size_t pointerB = (size_t)-1;

	do
	{
		pointerA++;
		pointerB++;
		while (a[pointerA] == ' ')
			pointerA++;
		while (a[pointerB] == ' ')
			pointerB++;
	} while (a[pointerA] != '\0' && b[pointerB] != '\0' && a[pointerA] == b[pointerB]);

	if (a[pointerA] == '\0' && b[pointerB] == '\0')
		return true;
	else
		return false;
}

bool compareString(const char* a, const char* b)
{
	size_t pointerA = (size_t)-1;
	size_t pointerB = (size_t)-1;
	char sub = 0;

	do
	{
		pointerA++;
		pointerB++;
		while (a[pointerA] == ' ')
			pointerA++;
		while (a[pointerB] == ' ')
			pointerB++;
		sub = a[pointerA] - b[pointerB];
	} while (a[pointerA] != '\0' && b[pointerB] != '\0' && (sub == 0 || sub == 'A' - 'a' || sub == 'a' - 'A'));

	if (a[pointerA] == '\0' && b[pointerB] == '\0')
		return true;
	else
		return false;
}

size_t findString(const char* buffer, const char ch, size_t bufferLenght)
{
	if (bufferLenght == 0)
		bufferLenght = strlen(buffer);

	size_t position = 0;

	while (position < bufferLenght && buffer[position] != ch)
	{
		position++;
	}

	return position;
}

size_t stoa(size_t size, char* buffer, size_t bufferSize)
{
	// 特殊情况
	if (size == 0)
	{
		if (bufferSize >= 2)
		{
			buffer[0] = '0';
			buffer[1] = '\0';
			return 1;
		}
		else
		{
			return 0;
		}
	}

	// 寻找长度
	size_t num = size;
	size_t pointer = 0;
	while (num != 0)
	{
		num /= 10;
		pointer++;
	}

	if (pointer >= bufferSize)
	{
		return 0;
	}
	size_t ret = pointer;

	buffer[pointer] = '\0';
	pointer--;

	while (size != 0)
	{
		buffer[pointer] = '0' + (size % 10);
		size /= 10;
		pointer--;
	}
	return ret;
}

size_t atos(char* buffer)
{
	size_t ret = 0;

	size_t bufferSize = strlen(buffer);

	size_t i = 0;
	while (i < bufferSize && buffer[i] == ' ')
		; // 跳过空字符
	while (i < bufferSize && '0' <= buffer[i] && buffer[i] <= '9')
	{
		ret *= 10;
		ret += buffer[i] - '0';
		i++;
	}

	return ret;
}

size_t atos(char* buffer, size_t bufferSize)
{
	size_t ret = 0;

	size_t i = 0;
	while (i < bufferSize && buffer[i] == ' ')
		; // 跳过空字符
	while (i < bufferSize && '0' <= buffer[i] && buffer[i] <= '9')
	{
		ret += buffer[i] - '0';
		ret *= 10;
	}

	return ret;
}

// HttpStringPair


HttpStringPair::HttpStringPair(const char* name, const char* value)
{
	size_t lenght;
	lenght = strlen(name) + 1;
	delete[] this->name;
	if (name != nullptr)
	{
		this->name = new char[lenght];
		memcpy(this->name, name, lenght);
	}
	else
	{
		this->name = nullptr;
	}

	lenght = strlen(value) + 1;
	delete[] this->value;
	if (value != nullptr)
	{
		this->value = new char[lenght];
		memcpy(this->value, value, lenght);
	}
	else
	{
		this->value = nullptr;
	}
}

HttpStringPair::HttpStringPair(const HttpStringPair& copy)
{
	size_t lenght;
	lenght = strlen(copy.name) + 1;
	delete[] name;
	if (copy.name != nullptr)
	{
		name = new char[lenght];
		memcpy(name, copy.name, lenght);
	}
	else
	{
		name = nullptr;
	}

	lenght = strlen(copy.value) + 1;
	delete[] value;
	if (copy.value != nullptr)
	{
		value = new char[lenght];
		memcpy(value, copy.value, lenght);
	}
	else
	{
		value = nullptr;
	}
}

HttpStringPair::HttpStringPair(HttpStringPair&& move)
{
	char* temp;
	temp = move.name;
	move.name = name;
	name = temp;

	temp = move.value;
	move.value = value;
	value = temp;
}

HttpStringPair::~HttpStringPair()
{
	delete[] name;
	name = nullptr;
	delete[] value;
	value = nullptr;
}

HttpStringPair& HttpStringPair::operator=(const HttpStringPair& stringPair)
{
	size_t lenght;
	lenght = strlen(stringPair.name) + 1;
	delete[] name;
	if (stringPair.name != nullptr)
	{
		name = new char[lenght];
		memcpy(name, stringPair.name, lenght);
	}
	else
	{
		name = nullptr;
	}

	lenght = strlen(stringPair.value) + 1;
	delete[] value;
	if (stringPair.value != nullptr)
	{
		value = new char[lenght];
		memcpy(value, stringPair.value, lenght);
	}
	else
	{
		value = nullptr;
	}

	return *this;
}

HttpStringPair& HttpStringPair::operator=(HttpStringPair&& stringPair)
{
	char* temp;
	temp = stringPair.name;
	stringPair.name = name;
	name = temp;

	temp = stringPair.value;
	stringPair.value = value;
	value = temp;

	return *this;
}

void HttpStringPair::swap(HttpStringPair& swap)
{
	char* temp;
	temp = swap.name;
	swap.name = name;
	name = temp;

	temp = swap.value;
	swap.value = value;
	value = temp;
}

void HttpStringPair::copy(const HttpStringPair& copy)
{
	size_t lenght;
	lenght = strlen(copy.name) + 1;
	delete[] name;
	if (copy.name != nullptr)
	{
		name = new char[lenght];
		memcpy(name, copy.name, lenght);
	}
	else
	{
		name = nullptr;
	}

	lenght = strlen(copy.value) + 1;
	delete[] value;
	if (copy.value != nullptr)
	{
		value = new char[lenght];
		memcpy(value, copy.value, lenght);
	}
	else
	{
		value = nullptr;
	}
}

void HttpStringPair::clear()
{
	delete[] name;
	name = nullptr;
	delete[] value;
	value = nullptr;
}

void HttpStringPair::setName(const char* name)
{
	size_t lenght;
	lenght = strlen(name) + 1;
	delete[] this->name;
	if (name != nullptr)
	{
		this->name = new char[lenght];
		memcpy(this->name, name, lenght);
	}
	else
	{
		this->name = nullptr;
	}
}

void HttpStringPair::setValue(const char* value)
{
	size_t lenght;
	lenght = strlen(value) + 1;
	delete[] this->value;
	if (value != nullptr)
	{
		this->value = new char[lenght];
		memcpy(this->value, value, lenght);
	}
	else
	{
		this->value = nullptr;
	}
}

const char* HttpStringPair::getName()
{
	return name;
}

const char* HttpStringPair::getValue()
{
	return value;
}

// HttpPairs

template <class T>
HttpPairs<T>::~HttpPairs()
{
	delete[] stringPairs;
	stringPairs = nullptr;
	stringPairsLenght = 0;
	stringPairsPointer = 0;
}

template <class T>
void HttpPairs<T>::setNumber(HttpStringPairSize_t number)
{
	if (number == 0)
	{
		// 仅删除
		delete[] stringPairs;
		stringPairs = nullptr;
		stringPairsLenght = 0;
		stringPairsPointer = 0;
		return;
	}

	// 判断是否足够
	if (number <= stringPairsLenght)
	{
		// 无需更改
		return;
	}

	// 需要重新申请内存
	T* newPairs = new T[number];
	for (HttpStringPairSize_t i = 0; i < stringPairsPointer; i++)
	{
		newPairs[i].swap(stringPairs[i]);
	}
	delete[] stringPairs;
	stringPairs = newPairs;
	stringPairsLenght = number;
}

template <class T>
HttpStringPairSize_t HttpPairs<T>::getNumber()
{
	return stringPairsPointer;
}

template <class T>
void HttpPairs<T>::clear()
{
	for (HttpStringPairSize_t i = 0; i < stringPairsPointer; i++)
	{
		stringPairs[i].clear();
	}
	stringPairsPointer = 0;
}

template <class T>
void HttpPairs<T>::add(const T& pair)
{
	// 检查有无剩余
	if (stringPairsPointer >= stringPairsLenght)
	{
		setNumber(stringPairsPointer + HttpStringPairsAddSpeed);
	}
	stringPairs[stringPairsPointer] = pair;
	stringPairsPointer++;
}

template <class T>
void HttpPairs<T>::add(T&& pair)
{
	// 检查有无剩余
	if (stringPairsPointer >= stringPairsLenght)
	{
		setNumber(stringPairsPointer + HttpStringPairsAddSpeed);
	}
	stringPairs[stringPairsPointer].swap(pair);
	stringPairsPointer++;
}

template <class T>
bool HttpPairs<T>::set(HttpStringPairSize_t index, const T& stringPair)
{
	if (index >= stringPairsPointer)
		return false;

	stringPairs[index] = stringPair;
	return true;
}

template <class T>
T& HttpPairs<T>::get(HttpStringPairSize_t index)
{
	return stringPairs[index];
}

template <class T>
T& HttpPairs<T>::operator[](HttpStringPairSize_t index)
{
	return stringPairs[index];
}

template class HttpPairs<HttpCookie>;
template class HttpPairs<HttpStringPair>;

//HttpCookie

HttpCookie::HttpCookie(const char* name, const char* value, uint32_t maxAge, const char* path) : HttpStringPair(name, value)
{
	this->maxAge = maxAge;

	if (path != nullptr)
	{
		size_t lenght = strlen(path) + 1;
		delete[] this->path;
		this->path = new char[lenght];
		memcpy(this->path, path, lenght);
	}
	else
	{
		delete[] this->path;
		this->path = nullptr;
	}
}

HttpCookie::HttpCookie(const HttpCookie& copy) : HttpStringPair(copy)
{
	maxAge = copy.maxAge;

	size_t lenght = strlen(copy.path) + 1;
	delete[] this->path;
	this->path = new char[lenght];
	memcpy(this->path, copy.path, lenght);
}

HttpCookie::HttpCookie(HttpCookie&& move) :HttpStringPair(move)
{
	uint32_t age = this->maxAge;
	this->maxAge = move.maxAge;
	move.maxAge = age;

	char* const temp = this->path;
	this->path = move.path;
	move.path = temp;
}

HttpCookie::~HttpCookie()
{
	delete[] path;
	path = nullptr;
}

HttpCookie& HttpCookie::operator=(const HttpCookie& copy)
{
	HttpStringPair::copy(copy);

	maxAge = copy.maxAge;

	if (copy.path != nullptr)
	{
		size_t lenght = strlen(copy.path) + 1;
		delete[] this->path;
		this->path = new char[lenght];
		memcpy(this->path, copy.path, lenght);
	}
	else
	{
		delete[] this->path;
		this->path = nullptr;
	}

	return *this;
}

void HttpCookie::clear()
{
	HttpStringPair::clear();
	maxAge = 0;
	delete[] path;
	path = nullptr;
}

void HttpCookie::setMaxAge(uint32_t age)
{
	maxAge = age;
}

uint32_t HttpCookie::getMaxAge()
{
	return maxAge;
}

void HttpCookie::setPath(const char* path)
{
	size_t lenght = strlen(path) + 1;
	delete[] this->path;
	this->path = new char[lenght];
	memcpy(this->path, path, lenght);
}

const char* HttpCookie::getPath()
{
	return path;
}

// httpCookies

HttpStringPairSize_t HttpCookies::receive(char* buffer)
{
	size_t bufferLenght = strlen(buffer);
	char* bufferEnd = buffer + bufferLenght;

	char* name = buffer;
	char* value = buffer;
	size_t end = 0;

	clear();

	while (value < bufferEnd)
	{
		while (name[0] == ' ')
			name++;
		end = findString(name, '=', bufferLenght);
		if (end == bufferLenght)
		{
			break;
		}

		name[end] = '\0';
		value = name + end + 1;

		while (value[0] == ' ')
			value++;
		end = findString(value, ';', bufferLenght);
		value[end] = '\0';

		add({ name, value });
		value += end + 1;
		name = value;
	}

	return getNumber();
}

void HttpCookies::send(OSocketStream& socketStream, bool cookieSet)
{
	//[问题] 需要确认缓冲区，防止只发送部分数据

	const char* buffer = nullptr;
	HttpCookie* target = nullptr;
	HttpStringPairSize_t now = 0;
	HttpStringPairSize_t totolNumber = getNumber();
	size_t lenght = 0;

	if (cookieSet)
	{
		//set-cookie
		while (now < totolNumber)
		{
			target = &get(now);

			socketStream.write(HttpHeadName::SetCookie, sizeof(HttpHeadName::SetCookie) - 1);
			socketStream.put(':');

			buffer = target->getName();
			lenght = strlen(buffer);
			socketStream.write(buffer, lenght);
			socketStream.put('=');

			buffer = target->getValue();
			lenght = strlen(buffer);
			socketStream.write(buffer, lenght);

			if (target->getMaxAge() != 0)
			{
				socketStream.put(';');
				socketStream.write(HttpHeadName::MaxAge, sizeof(HttpHeadName::MaxAge) - 1);
				socketStream.put('=');

				char buffer[21] = "";
				lenght = stoa((size_t)target->getMaxAge(), buffer, sizeof(buffer));
				socketStream.write(buffer, lenght);
			}

			buffer = target->getPath();
			if (buffer != nullptr)
			{
				socketStream.put(';');
				socketStream.write(HttpHeadName::Path, sizeof(HttpHeadName::Path) - 1);
				socketStream.put('=');

				lenght = strlen(buffer);
				socketStream.write(buffer, lenght);
			}

			socketStream.put('\n');
			now++;
		}
	}
	else
	{
		//cookie
		socketStream.write(HttpHeadName::Cookie, sizeof(HttpHeadName::Cookie) - 1);
		socketStream.put(':');
		while (now < totolNumber)
		{
			target = &get(now);

			buffer = target->getName();
			lenght = strlen(buffer);
			socketStream.write(buffer, lenght);
			socketStream.put('=');

			buffer = target->getValue();
			lenght = strlen(buffer);
			socketStream.write(buffer, lenght);
			socketStream.put(';');

			now++;
		}
	}
}

// httpHeads

HttpCookies& HttpHeads::getCookies()
{
	return cookies;
}

HttpStringPairSize_t HttpHeads::receive(ISocketStream& socketStream)
{
	char buffer[HttpHeadsBufferLenght] = "";
	size_t bufferPointer = 0;
	size_t dividePointer = 0;
	bool flag = true;
	size_t pairCount = 0;

	clear();
	socketStream.ignoreVoid();
	do
	{
		bufferPointer = socketStream.getline(buffer, sizeof(buffer), '\n');
		if (bufferPointer <= 1) //"\r"
		{
			// 结束
			flag = false;
			break;
		}
		dividePointer = findString(buffer, ':', bufferPointer);
		if (dividePointer == bufferPointer)
		{
			// 未找到分隔符
			flag = false;
			break;
		}

		// 断开前后
		buffer[dividePointer] = '\0';
		dividePointer++;

		// 寻找起点
		while (buffer[dividePointer] == ' ')
		{
			dividePointer++;
		}

		// 寻找终点
		bufferPointer--; // 跳过\0
		while (buffer[bufferPointer] == ' ' || buffer[bufferPointer] == '\r')
		{
			bufferPointer--;
		}
		bufferPointer++;
		buffer[bufferPointer] = '\0';

		// 判断特殊行
		if (compareString(buffer, HttpHeadName::Cookie))
		{
			// Cookie
			cookies.receive(buffer + dividePointer);
			continue;
		}
		else if (compareString(buffer, HttpHeadName::ContentLenght))
		{
			// Content-Lenght
			contentLenght = atos(buffer + dividePointer);
			continue;
		}
		else
		{
			// 添加键值对
			add({ buffer, buffer + dividePointer });
			pairCount++;
		}
	} while (flag);

	return pairCount;
}

void HttpHeads::send(OSocketStream& socketStream, bool cookieSet)
{
	//[问题] 需要确认缓冲区，防止只发送部分数据

	const char* buffer = nullptr;
	HttpStringPairSize_t now = 0;
	HttpStringPairSize_t totolNumber = getNumber();
	size_t lenght = 0;
	while (now < totolNumber)
	{
		buffer = get(now).getName();
		lenght = strlen(buffer);
		socketStream.write(buffer, lenght);
		socketStream.put(':');

		buffer = get(now).getValue();
		lenght = strlen(buffer);
		socketStream.write(buffer, lenght);
		socketStream.put('\n');

		now++;
	}

	// Cookie
	cookies.send(socketStream, cookieSet);

	// Content-Lenght
	// if (contentLenght != 0)
	{
		char buffer[21] = "";
		size_t bufferLenght = 0;
		bufferLenght = stoa(contentLenght, buffer, sizeof(buffer));

		socketStream.write(HttpHeadName::ContentLenght, sizeof(HttpHeadName::ContentLenght) - 1);
		socketStream.put(':');
		socketStream.write(buffer, bufferLenght);
		socketStream.put('\n');
	}
}

// httpBase

HttpHeads& HttpBase::getHeads()
{
	return heads;
}

HttpCookies& HttpBase::getCookies()
{
	return cookies;
}

size_t HttpBase::getBodyLenght()
{
	return bodyLenght;
}

void HttpBase::setBodyLenght(size_t lenght)
{
	bodyLenght = lenght;
}

bool HttpBase::isBodyRecieved()
{
	return bodyIsRecieved;
}

bool HttpBase::isBodyFile()
{
	return bodyIsFile;
}

void* HttpBase::getBody()
{
	return body;
}

void HttpBase::setBody(void* body)
{
	bodyIsFile = false;
	this->body = body;
}

void HttpBase::setBody(HttpFileBodyType& file)
{
	bodyIsFile = true;
	body = &file;
}

// request

HttpRequest::~HttpRequest()
{
	delete[] path;
	path = nullptr;
}

void HttpRequest::receive(ISocketStream& socketStream)
{
	// method
	{
		char requestMethod[10] = "";

		socketStream.ignoreVoid();
		socketStream.getline(requestMethod, sizeof(requestMethod), ' ');
		method = getMethodFromName(requestMethod);
	}

	// url
	{
		char url[HttpUrlBufferLenght] = "";
		size_t lenght = 0;

		socketStream.ignoreVoid();
		socketStream.getline(url, sizeof(url), ' ');
		lenght = decodePercentUrl(url) + 1;
		delete[] path;
		path = new char[lenght];
		memcpy(path, url, lenght);
	}

	// http version
	{
		//"HTTP/1.1";
		constexpr size_t trashSize = 20;
		constexpr size_t readMaxSize = trashSize - 1;
		char trash[trashSize] = "";
		size_t readNumber = 0;
		bool skiping = true;

		socketStream.ignoreVoid();
		while (skiping)
		{
			readNumber = socketStream.getline(trash, sizeof(trash), '\n');
			skiping = (readNumber == readMaxSize);
		}
	}

	// head
	{
		heads.receive(socketStream);
	}

	// body
	{
		bodyIsFile = false;
		bodyIsRecieved = false;
		body = nullptr;
	}
}

void HttpRequest::send(OSocketStream& socketStream)
{
	// request line
	const char* method = getMethodNameFromMethod(this->method);
	socketStream.write(method, strlen(method));
	socketStream.put(' ');

	socketStream.write(path, strlen(path));
	socketStream.put(' ');

	socketStream.write(HttpVersion, HttpVersionSize);
	socketStream.put('\n');

	// head
	this->heads.send(socketStream, false);
	socketStream.put('\n');

	// body
	if (!bodyIsFile)
	{
		socketStream.write((char*)this->body, getBodyLenght());
	}
	else
	{
		char buffer[HttpBodySendingBufferLenght];
		HttpFileBodyType& file = *(HttpFileBodyType*)body;
		size_t i = bodyLenght;
		while (i > 0)
		{
			//printf("[debug] HttpRequest: sending file, byte left = %u\n", i);
			i -= file.read(buffer, HttpBodySendingBufferLenght);
			socketStream.write(buffer, HttpBodySendingBufferLenght);
			vTaskDelay(1);
		}
	}
}

void HttpRequest::setMethod(HttpMethod method)
{
	this->method = method;
}

HttpMethod HttpRequest::getMethod()
{
	return method;
}

void HttpRequest::setPath(const char* path)
{
	size_t lenght = strlen(path) + 1;
	delete[] this->path;
	this->path = new char[lenght];
	memcpy(this->path, path, lenght);
}

const char* HttpRequest::getPath()
{
	return path;
}

size_t HttpRequest::decodePercentUrl(char* url)
{
	size_t read = 0;
	size_t write = 0;

	//跳过没有%的地方
	while (url[read] != '%' && url[read] != '\0') read++;

	write = read;

	while (url[read] != '\0')
	{
		if (url[read] == '%')
		{
			// %20
			// s
			url[write] = decodePercent(url[read + 1], url[read + 2]);
			read += 3;
			write++;
		}
		else
		{
			url[write] = url[read];
			read++;
			write++;
		}
	}
	url[write] = '\0';
	return write;
}

char HttpRequest::decodePercent(char A, char B)
{
	char ret = 0;

	switch (A)
	{
	default:
	case '0':
		//ret += 0;
		break;
	case '1':
		ret += 1;
		break;
	case '2':
		ret += 2;
		break;
	case '3':
		ret += 3;
		break;
	case '4':
		ret += 4;
		break;
	case '5':
		ret += 5;
		break;
	case '6':
		ret += 6;
		break;
	case '7':
		ret += 7;
		break;
	case '8':
		ret += 8;
		break;
	case '9':
		ret += 9;
		break;
	case 'A':
	case 'a':
		ret += 10;
		break;
	case 'B':
	case 'b':
		ret += 11;
		break;
	case 'C':
	case 'c':
		ret += 12;
		break;
	case 'D':
	case 'd':
		ret += 13;
		break;
	case 'E':
	case 'e':
		ret += 14;
		break;
	case 'F':
	case 'f':
		ret += 15;
		break;
	}
	ret <<= 4;

	switch (B)
	{
	default:
	case '0':
		//ret += 0;
		break;
	case '1':
		ret += 1;
		break;
	case '2':
		ret += 2;
		break;
	case '3':
		ret += 3;
		break;
	case '4':
		ret += 4;
		break;
	case '5':
		ret += 5;
		break;
	case '6':
		ret += 6;
		break;
	case '7':
		ret += 7;
		break;
	case '8':
		ret += 8;
		break;
	case '9':
		ret += 9;
		break;
	case 'A':
	case 'a':
		ret += 10;
		break;
	case 'B':
	case 'b':
		ret += 11;
		break;
	case 'C':
	case 'c':
		ret += 12;
		break;
	case 'D':
	case 'd':
		ret += 13;
		break;
	case 'E':
	case 'e':
		ret += 14;
		break;
	case 'F':
	case 'f':
		ret += 15;
		break;
	}

	return ret;
}

// respond

HttpRespond::~HttpRespond()
{
	delete[] reason;
	reason = nullptr;
}

void HttpRespond::receive(ISocketStream& socketStream)
{
	// http version
	{
		//"HTTP/1.1";
		constexpr size_t trashSize = 20;
		constexpr size_t readMaxSize = trashSize - 1;
		char trash[trashSize] = "";
		size_t readNumber = 0;
		bool skiping = true;

		socketStream.ignoreVoid();
		while (skiping)
		{
			readNumber = socketStream.getline(trash, sizeof(trash), ' ');
			skiping = (readNumber == readMaxSize);
		}
	}

	//status
	{
		char buffer[4] = "200";
		socketStream.ignoreVoid();
		socketStream.getline(buffer, sizeof(buffer), ' ');
		status = (HttpStatus)atos(buffer);
	}

	//reason
	{
		char buffer[HttpReasonBufferLenght] = "";
		socketStream.ignoreVoid();
		size_t lenght = socketStream.getline(buffer, sizeof(buffer), '\n') + 1;
		delete[] reason;
		reason = new char[lenght];
		memcpy(buffer, reason, lenght);
	}

	//head
	{
		heads.receive(socketStream);
	}

	// body
	{
		bodyIsFile = false;
		bodyIsRecieved = false;
		body = nullptr;
	}
}

void HttpRespond::send(OSocketStream& socketStream)
{
	//version
	{
		socketStream.write(HttpVersion, HttpVersionSize);
		socketStream.put(' ');
	}

	//status
	{
		size_t count = 0;
		char buffer[4] = "200";
		count = stoa((size_t)status, buffer, sizeof(buffer));
		socketStream.write(buffer, count);
		socketStream.put(' ');
	}

	//reason
	{
		const char* target = nullptr;
		if (reason != nullptr) target = reason;
		else target = getReasonFromStatus(status);

		size_t lenght = strlen(target);
		socketStream.write(target, lenght);
		socketStream.put('\n');
	}

	//head
	{
		heads.send(socketStream, true);
		socketStream.put('\n');
	}

	//body
	if (!bodyIsFile)
	{
		socketStream.write((char*)this->body, getBodyLenght());
	}
	else
	{
		char buffer[HttpBodySendingBufferLenght];
		HttpFileBodyType& file = *(HttpFileBodyType*)body;
		size_t i = bodyLenght;
		while (i > 0)
		{
			//printf("[debug] HttpRespond: sending file, byte left = %u\n", i);
			i -= file.read(buffer, HttpBodySendingBufferLenght);
			socketStream.write(buffer, HttpBodySendingBufferLenght);
			vTaskDelay(1);
		}
	}

#if false
		printf("HttpRespond::send: stack high water = %u\n", uxTaskGetStackHighWaterMark(nullptr));
#endif
}

void HttpRespond::setStatus(HttpStatus status)
{
	this->status = status;
}

HttpStatus HttpRespond::getStatus()
{
	return status;
}

void HttpRespond::setReason(const char* reason)
{
	if (reason == nullptr)
	{
		delete[] this->reason;
		this->reason = nullptr;
		return;
	}

	size_t lenght = strlen(reason) + 1;
	delete[] this->reason;
	this->reason = new char[lenght];
	memcpy(this->reason, reason, lenght);
}

// function

const char* getReasonFromStatus(const HttpStatus status)
{
	switch (status)
	{
	case HttpStatus::Ok:
		return HttpReason::Ok;
	case HttpStatus::BadRequest:
		return HttpReason::BadRequest;
	case HttpStatus::Unauthorized:
		return HttpReason::Unauthorized;
	case HttpStatus::Forbidden:
		return HttpReason::Forbidden;
	case HttpStatus::NotFound:
		return HttpReason::NotFound;
	case HttpStatus::InternalServerError:
		return HttpReason::InternalServerError;
	default:
		return HttpReason::Null;
	}
}

const char* getMethodNameFromMethod(const HttpMethod method)
{
	switch (method)
	{
	default:
	case HttpMethod::Get:
		return HttpMethodName::Get;
	case HttpMethod::Post:
		return HttpMethodName::Post;
	case HttpMethod::Put:
		return HttpMethodName::Put;
	case HttpMethod::Delete:
		return HttpMethodName::Delete;
	}
}

HttpMethod getMethodFromName(const char* name)
{
	switch (name[0])
	{
	case 'G':
	case 'g':
		return HttpMethod::Get;
	case 'P':
	case 'p':
	default:
		switch (name[1])
		{
		case 'O':
		case 'o':
		default:
			return HttpMethod::Post;
		case 'U':
		case 'u':
			return HttpMethod::Put;
		}
	case 'D':
	case 'd':
		return HttpMethod::Delete;
	}
}

HttpContentType getContentTypeFromPath(const char* path, size_t length)
{
	if ((length > 4 && path[length - 4] == '.' && path[length - 3] == 'h' && path[length - 2] == 't' && path[length - 1] == 'm') ||
		(length > 5 && path[length - 5] == '.' && path[length - 4] == 'h' && path[length - 3] == 't' && path[length - 2] == 'm' && path[length - 1] == 'l'))
		return HttpContentType::Html;
	if ((length > 4 && path[length - 4] == '.' && path[length - 3] == 'c' && path[length - 2] == 's' && path[length - 1] == 's'))
		return HttpContentType::Css;
	if ((length > 3 && path[length - 3] == '.' && path[length - 2] == 'j' && path[length - 1] == 's'))
		return HttpContentType::Javascript;
	return HttpContentType::Other;
}

HttpContentType getContentTypeFromPath(const char* path)
{
	size_t length = strlen(path);
	return getContentTypeFromPath(path, length);
}

const char* getContentTypeNameFromContentType(HttpContentType type)
{
	switch (type)
	{
	case HttpContentType::Html:
		return HttpContentTypeName::Html;
		break;
	case HttpContentType::Javascript:
		return HttpContentTypeName::Javascript;
		break;
	case HttpContentType::Css:
		return HttpContentTypeName::Css;
		break;
	case HttpContentType::Other:
	default:
		return HttpContentTypeName::Other;
		break;
	}
	return HttpContentTypeName::Other;
}
