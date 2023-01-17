# MiniAsio
This is currently only an OOP wrapper over Unix listen and poll. Requires at least C++14, but there are some vague plans to make this work with coroutines, which would increase the requirement to C++20.

## Usage
There is currently one class, `Listener`, that represents the most barebones OOP usage of listen. It needs an abstract factory that produces instances for connections, that must inherit from `ISession`:

```C++
struct SimpleSession : MiniAsio::ISession {
	std::array<uint8_t, 2048> readBuffer = {};
	
	// Getter to get the buffer
	MiniAsio::span<uint8_t> receiveBuffer() override {
		return readBuffer;
	}
	
	// Optional function to call when a session is created
	void sessionCreated() override {
		std::cout << "Incoming connection" << std::endl;
	}
	
	// Called when some data is received, receives part of the buffer with valid data
	void dataReceived(MiniAsio::span<uint8_t>) override {
		static constexpr std::string_view response = "HTTP/1.1 200 OK\r\nContent-Length: 89\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE html><html><head><title>DUMMY PAGE</title></head><body>NO CONTENT</body></html>";
		// Has a method for sending data back
		sendBuffer(MiniAsio::span(reinterpret_cast<const uint8_t*>(response.data()), response.size()));
	}

	// Destructor called on disconnect
	~SimpleSession() {
		std::cout << "Connection disconnected" << std::endl;
	}
};
```

The child class of `ISession` can use methods `sendBuffer()` to send data and `closeConnection()` to shut down the session (the function must return after calling it, as it deletes the instance). It uses a simplified replica of `std::span` that does not require C++20.

The factory for these only has to contain a method for returning these instances:
```C++
struct SimpleSessionProvider : MiniAsio::ISessionProvider {
	std::unique_ptr<MiniAsio::ISession> makeSession() override {
		return std::make_unique<SimpleSession>();
	}
};
```

The constructor of `Listener` takes a reference to an instance of `ISessionProvider` and port number and keeps the listening and responding to requests in a separate thread until it's destroyed.

```C++
SimpleSessionProvider provider = {};
MiniAsio::Listener listener(provider, 8080); // Port number is 8080
std::this_thread::sleep_for(std::chrono::minutes(10)); // Don't exit immediately
```