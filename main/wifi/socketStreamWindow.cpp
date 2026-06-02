#include "socketStreamWindow.hpp"

Mutex SocketStreamWindow::sharedMutex;
size_t SocketStreamWindow::enabledCount = 0;

void SocketStreamWindow::setIndex(size_t index)
{
	this->index = index;
}

size_t SocketStreamWindow::getIndex()
{
	return index;
}

bool SocketStreamWindow::isAviliable()
{
	return aviliable;
}

bool SocketStreamWindow::check()
{
	if (aviliable) return false;
	if (!socketStream.isGood()) closeSelf();
	return socketStream.check();
}

bool SocketStreamWindow::enable()
{
	if (aviliable) return false;
	return mutex.try_lock();
}

void SocketStreamWindow::disable()
{
	mutex.unlock();
	return;
}

bool SocketStreamWindow::setSocket(Socket socket)
{
	if (!aviliable) return false;
	if (!mutex.try_lock()) return false;
	// mutex locked
	socketStream.setSocket(socket);
	aviliable = false;

	sharedMutex.lock();
	enabledCount++;
	sharedMutex.unlock();

	mutex.unlock();
	return true;
}

IOSocketStream& SocketStreamWindow::getSocketStream()
{
	return socketStream;
}

bool SocketStreamWindow::closeSocket(bool blocked)
{
	if (!blocked)
	{
		if (!mutex.try_lock()) return false;
	}
	else mutex.lock();
	closeSelf();
	mutex.unlock();
	return true;
}

size_t SocketStreamWindow::getEnabledCount()
{
	return enabledCount;
}

void SocketStreamWindow::closeSelf()
{
	//printf("SocketStreamWindow::close\n");
	socketStream.close();
	aviliable = true;
	sharedMutex.lock();
	enabledCount--;
	sharedMutex.unlock();
}
