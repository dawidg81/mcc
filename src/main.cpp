#include "Logger.hpp"
#include "Socket.hpp"

#include <winsock.h>

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
	}

	return 0;
}

