#ifndef MINIASIO_HPP
#define MINIASIO_HPP

#include <array>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <poll.h>

namespace MiniAsio {

template <typename T>
class span {
	T* start = nullptr;
	size_t count = 0;

public:
	template <size_t Size>
	constexpr span(std::array<T, Size>& from) : start(from.data()), count(Size) {}
	constexpr span(T* start, size_t count)  : start(start), count(count) {}
	constexpr span(T* start, T* end)  : start(start), count(end - start) {}

	size_t size() const {
		return count;
	}
	T* begin() {
		return start;
	}
	T* data() {
		return start;
	}
	T* end() {
		return start + count;
	}
};

struct IListener {
	struct FileDescriptor {
		int descriptor = 0;
	};

	virtual void sendBytes(FileDescriptor socket, span<const uint8_t> sent) = 0;
	virtual void destroySession(int index) = 0;
};

class ISession {
	IListener::FileDescriptor socket = {};
	int index = 0;
	IListener* listener = nullptr;

	void setup(IListener::FileDescriptor newSocket, int newIndex, IListener* newListener) {
		socket = newSocket;
		index = newIndex;
		listener = newListener;
	}
	friend class Listener;

protected:
	virtual span<uint8_t> receiveBuffer() = 0;
	virtual void sessionCreated() {};
	virtual void dataReceived(span<uint8_t>) {}

	void sendBuffer(span<const uint8_t> sent) {
		listener->sendBytes(socket, sent);
	}
	void closeConnection() {
		listener->destroySession(index);
	}

public:
	virtual ~ISession() = default;
};

struct ISessionProvider {
	virtual std::unique_ptr<ISession> makeSession() = 0;
	virtual ~ISessionProvider() = default;
};

class Listener : public IListener {
	ISessionProvider& provider;
	std::vector<std::unique_ptr<ISession>> sessions = {};
	std::thread worker;
	std::atomic_bool done = false;
	int listenSocketHandle = 0;
	std::vector<pollfd> pollDescriptors;

	constexpr static int PollInFlag = POLLIN;

	void sendBytes(FileDescriptor socket, span<const uint8_t> sent) override {
		if (send(socket.descriptor, sent.data(), sent.size(), 0) < 0) {
			throw std::runtime_error("Failed to write data");
		}
	}

	void createSession(FileDescriptor socket) {
		std::unique_ptr<ISession> session = provider.makeSession();
		session->setup(socket, sessions.size(), this);
		session->sessionCreated();
		sessions.emplace_back(std::move(session));
		pollfd descriptor = {};
		descriptor.fd = socket.descriptor;
		descriptor.events = PollInFlag;
		pollDescriptors.push_back(descriptor);
	}

	void destroySession(int index) override {
		close(sessions[index]->socket.descriptor);
		if (index == int(sessions.size() - 1)) {
			sessions.pop_back();
			pollDescriptors.pop_back();
		} else {
			sessions.back()->index = sessions[index]->index;
			std::swap(sessions.back(), sessions[index]);
			sessions.pop_back();
			pollDescriptors[index + 1] = pollDescriptors.back();
			pollDescriptors.pop_back();
		}
	}

	void pollLoop() {
		while (!done) {
			int eventCount = poll(pollDescriptors.data(), pollDescriptors.size(), 1000);
			if (eventCount < 0) {
				return; // Poll failed, can't handle this
			}
			if (eventCount > 0) {
				if (pollDescriptors[0].revents != 0) {
					if (pollDescriptors[0].revents == PollInFlag) {
						int newDescriptor = 0;
						do {
							newDescriptor = accept(listenSocketHandle, nullptr, nullptr);
							if (newDescriptor < 0) break;
							createSession(FileDescriptor{newDescriptor});
						}  while (true);
					}
					pollDescriptors[0].revents = 0;
				}
				for (size_t i = 1; i < pollDescriptors.size(); i++) {
					if (pollDescriptors[i].revents != 0) {
						ISession& session = *sessions[i - 1];
						span<uint8_t> buffer = session.receiveBuffer();
						int received = recv(session.socket.descriptor, buffer.data(), buffer.size(), 0);
						if (received > 0) {
							session.dataReceived(span(buffer.data(), received));
						} else if (received == 0) {
							i--;
							destroySession(i);
							continue;
						}
						// Otherwise it's been used incorrectly
					}
				}
			}
		}
	}

public:
	Listener(ISessionProvider& provider, uint16_t port) : provider(provider) {

		listenSocketHandle = socket(AF_INET, SOCK_STREAM, 0);
		if (listenSocketHandle < 0) {
			throw std::runtime_error("Could not open socket");
		}

		constexpr static int reuseAddress = 1;
		setsockopt(listenSocketHandle, SOL_SOCKET, SO_REUSEADDR, &reuseAddress,
			sizeof(reuseAddress));

		{
			int options = fcntl(listenSocketHandle, F_GETFL);
			if (options < 0) {
				throw std::runtime_error("Could get socket options");
			}
			options = (options | O_NONBLOCK);
			if (fcntl(listenSocketHandle, F_SETFL, options) < 0) {
				throw std::runtime_error("Could make socket nonblocking");
			}
		}

		sockaddr_in serverAddress = {};
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
		serverAddress.sin_port = (port >> 8) + int16_t(port << 8); // Swap bytes to big endian
		if (bind(listenSocketHandle, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0 ) {
			close(listenSocketHandle);
			throw std::runtime_error("Could not bind socket to port");
		}

		listen(listenSocketHandle, 100);
		pollfd listenDescriptor = {};
		listenDescriptor.fd = listenSocketHandle;
		listenDescriptor.events = PollInFlag;
		pollDescriptors.push_back(listenDescriptor);

		worker = std::thread(&Listener::pollLoop, this);
	}
	~Listener() {
		done = true;
		if (worker.joinable())
			worker.join();
	}
};

}

#endif // MINIASIO_HPP
