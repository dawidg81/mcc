#include "Logger.hpp"
#include "Socket.hpp"

#include <winsock2.h>

using namespace std;

Logger log;

int main() {
	log.raw("mcc v0.0.0");
	Socket socket;
	socket.winInit();
	socket.winBind();
	socket.winListen();

	socket.running = true;
	while(socket.running){
		SOCKET clientSocket = socket.winAccept();
		if(clientSocket == INVALID_SOCKET) continue;
		/*
		 * RECEIVING CLIENT PACKET
		 * 1. Receive raw bytes
		 * 2. Parse bytes from packet
		 * 3. Put into components understandable for server
		 */
		uint8_t buffer[131] = recv(clientSocket, buffer, sizeof(buffer), 0);
	}

	return 0;
}

