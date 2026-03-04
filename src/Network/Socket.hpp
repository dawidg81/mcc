#include <winsock2.h>
#include "../Core/Logger.hpp"

#pragma once

#define NET_SOCK_ADDR "0.0.0.0"
#define NET_SOCK_PORT 25565

class Socket
{
private:
	Logger log;

public:
	int winInit()
	{
		WSADATA wsaData;
		int result = WSAStartup( MAKEWORD(2, 2), & wsaData );
		if (result != 0) log.err("Network.Socket.winInit: Initialization error");

		log.info("Network.Socket.winInit: Socket initialized");

		SOCKET mainSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		if (mainSocket == INVALID_SOCKET) {
			log.err("Network.Socket.winInit: Fatal error: Error creating socket");
			log.err(WSAGetLastError());
			WSACleanup();
			return 1;
		}

		log.info("Network.Socket.winInit: Socket created");

		sockaddr_in service;
		memset( & service, 0, sizeof(service) );
		service.sin_family = AF_INET;
		service.sin_addr.s_addr = inet_addr(NET_SOCK_ADDR);
		service.sin_port = htons(NET_SOCK_PORT);

		log.info("Network.Socket.winInit: Socket configured");

		if (bind(mainSocket, (SOCKADDR *) & service, sizeof(service)) == SOCKET_ERROR ) {
			log.err("Network.Socket.winInit: Fatal error: Bind failed");
			closesocket(mainSocket);
			return 1;
		}

		log.info("Network.Socket.winInit: Socket bound to address " + NET_SOCK_ADDR + " on port " + string to_string(NET_SOCK_PORT) + ". Ready to listen for connections");

		return 0;
	}
};
