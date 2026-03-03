#pragma once
#include <winsock2.h>
#include <string>

class Socket {
private:
	SOCKET m_socket;

public:
	Socket();
	~Socket();

	int init();
	int bind(const std::string& address, uint16_t port);
	int listen(int backlog = SOMAXCONN);
	
};
