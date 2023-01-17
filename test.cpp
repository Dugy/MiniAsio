#include <iostream>
#include "MiniAsio.hpp"

struct SimpleSession : MiniAsio::ISession {
	std::array<uint8_t, 2048> readBuffer = {};
	MiniAsio::span<uint8_t> receiveBuffer() override {
		return readBuffer;
	}
	void sessionCreated() override {
		std::cout << "Incoming connection" << std::endl;
	}
	void dataReceived(MiniAsio::span<uint8_t>) override {
		static constexpr std::string_view response = "HTTP/1.1 200 OK\r\nContent-Length: 89\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE html><html><head><title>DUMMY PAGE</title></head><body>NO CONTENT</body></html>";
		sendBuffer(MiniAsio::span(reinterpret_cast<const uint8_t*>(response.data()), response.size()));
	}

	~SimpleSession() {
		std::cout << "Connection disconnected" << std::endl;
	}
};

struct SimpleSessionProvider : MiniAsio::ISessionProvider {
	std::unique_ptr<MiniAsio::ISession> makeSession() override {
		return std::make_unique<SimpleSession>();
	}
};

int main()
{
	SimpleSessionProvider provider = {};
	MiniAsio::Listener listener(provider, 8080);
	std::this_thread::sleep_for(std::chrono::minutes(10));
	return 0;
}
