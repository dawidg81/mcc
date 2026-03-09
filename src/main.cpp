#include "Logger.hpp"
#include "Socket.hpp"

#include <winsock2.h>
#include <string>

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
		 * This is where client handling starts.
		 * RECEIVING CLIENT PACKET
		 * 1. Receive raw bytes,
		 * 2. Parse bytes from packet, put into components understandable for server.
		 */
		char buffer[131] = {};
		int bytesRecv = recv(clientSocket, buffer, sizeof(buffer), 0);

		if(bytesRecv <= 0){
			log.err("No bytes received");
			closesocket(clientSocket);
			return 1;
		}

		uint8_t packID = buffer[0];
		uint8_t protVer = buffer[1];
		string username; username.assign(buffer + 2, 64);
		string verKey; verKey.assign(buffer + 66, 64);
		uint8_t unused = buffer[130];

		log.info(username + " connected");
	}

	return 0;
}

