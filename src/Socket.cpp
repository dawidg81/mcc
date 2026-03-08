#include "Socket.hpp"

sockaddr_in service;
int Socket::winInit()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
	log.err("Initialization error");
	return 1;
    }
    log.info("WSA initialized");

    mainSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (mainSocket == INVALID_SOCKET) {
	log.err("Error creating socket: " + std::to_string(WSAGetLastError()));
	WSACleanup();
	return 1;
    }
    log.info("Socket created");

    memset(&service, 0, sizeof(service));
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = inet_addr(NET_SOCK_ADDR);
    service.sin_port = htons(NET_SOCK_PORT);

    
    return 0;
}

int Socket::winBind()
{
	if (bind(mainSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
		log.err("Bind failed: " + std::to_string(WSAGetLastError()));
		closesocket(mainSocket);

		return 1;
	}
	return 0;
}

int Socket::winListen()
{
	if (listen(mainSocket, SOMAXCONN) == SOCKET_ERROR) {
		log.err("Listen failed: " + std::to_string(WSAGetLastError()));
		closesocket(mainSocket);
		return 1;
	}

	log.info("Listening on " + std::string(NET_SOCK_ADDR) + ":" + std::to_string(NET_SOCK_PORT));
	return 0;
}

SOCKET Socket::winAccept(){
	SOCKET clientSocket = accept(mainSocket, NULL, NULL);
	if(clientSocket == INVALID_SOCKET){
		log.err("Accept failed: " + std::to_string(WSAGetLastError()));
		return INVALID_SOCKET;
	}
	log.info("Client connected");
	return clientSocket;
}
