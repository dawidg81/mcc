#include "Logger.hpp"
#include "Socket.hpp"

#include <winsock2.h>
#include <string>

using namespace std;

Logger log;

auto writeMCString = [](char* buf, const string& str){
	memset(buf, ' ', 64);
	memcpy(buf, str.c_str(), min(str.size(), (size_t)64));
};

class Player {
public:
	string username;
	string verKey;
	bool isOP;

	Player(string uname, string verkey, bool op){
		username = uname;
		verKey = verkey;
		isOP = op;
	}
};

class Packet {
public:
	Player* recvPlayerId(SOCKET socket){
		char buffer[131] = {};
		int bytesRecv = recv(socket, buffer, sizeof(buffer), 0);

		if(bytesRecv <= 0){
			log.err("No bytes received");
			closesocket(socket);
			return nullptr;
		}

		// uint8_t packID = buffer[0];
		// uint8_t protVer = buffer[1];
		string username; username.assign(buffer + 2, 64);
		username.erase(username.find_first_of("\0 \t\r\n", 0, 6));
		string verKey; verKey.assign(buffer + 66, 64);
		uint8_t unused = buffer[130];

		log.info(username + " connected");
		return new Player(username, verKey, false);
	}

	void sendServerId(SOCKET socket, string name, string motd, char utype){
		char buffer[131] = {};

		buffer[0] = 0x00;
		buffer[1] = 0x07;
		writeMCString(buffer + 2, name);
		writeMCString(buffer + 66, motd);
		buffer[130] = utype;

		int bytesSent = send(socket, buffer, sizeof(buffer), 0);
		if(bytesSent != sizeof(buffer)){
			log.err("Failed to send buffer");
			closesocket(socket);
		}
	}
};

Packet pack;

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

		// Receive player identification
		Player* player = pack.recvPlayerId(clientSocket);
		if(player == nullptr){
			continue;
		}

		// send server identification (using the same buffer)
		string name = "MCC Testing";
		string motd = "Welcome, " + player->username + "!";
		char utype = player->isOP ? 0x64 : 0x00;

		pack.sendServerId(clientSocket, name, motd, utype);

		// Level initialization (just to debug server id packet)
		char levelinitbuff = 0x02;
		send(clientSocket, &levelinitbuff, sizeof(levelinitbuff), 0);

		// delete player;
	}

	return 0;
}

