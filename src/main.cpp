#include "Logger.hpp"
#include "Socket.hpp"

#include <winsock.h>

#define NET_SOCK_ADDR "0.0.0.0"
#define NET_SOCK_PORT 25565

using namespace std;

Logger log;

int main() {
	log.raw("mcc v0.0.0");
	Socket socket;
	socket.winInit();
	
	return 0;
}
