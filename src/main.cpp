#include <iostream>
#include <winsock.h>

#define NET_SOCK_ADDR "0.0.0.0"
#define NET_SOCK_PORT 25565

using namespace std;

class Network {
public:
	class Socket {
	public:
		int winInit() {
			WSADATA wsaData;
			int result = WSAStartup( MAKEWORD(2, 2), & wsaData );
			if (result != 0) cout << "Network.Socket.winInit: Error: Initialization error\n";

			cout << "Network.Socket.winInit: Socket initialized\n";

			SOCKET mainSocket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
			if (mainSocket == INVALID_SOCKET) {
				cout << "Network.Socket.winInit: Fatal error: Error creating socket: " << WSAGetLastError();
				WSACleanup();
				return 1;
			}

			cout << "Network.Socket.winInit: Socket created\n";

			sockaddr_in service;
			memset( & service, 0, sizeof(service) );
			service.sin_family = AF_INET;
			service.sin_addr.s_addr = inet_addr(NET_SOCK_ADDR);
			service.sin_port = htons(NET_SOCK_PORT);

			cout << "Network.Socket.winInit: Socket configured\n";

			if (bind(mainSocket, (SOCKADDR *) & service, sizeof(service)) == SOCKET_ERROR ) {
				cout << "Network.Socket.winInit: Fatal error: Bind failed\n";
				closesocket(mainSocket);
				return 1;
			}

			cout << "Network.Socket.winInit: Socket bound to address " << NET_SOCK_ADDR << " on port " << NET_SOCK_PORT << ". Ready to listen for connections\n";

			return 0;
		}
	};
};

int main() {
	cout << "mcc v0.0.0\n";
	
	return 0;
}
