#include "Logger.hpp"
#include "Socket.hpp"

#include <winsock2.h>
#include <string>
#include <zlib.h>
#include <vector>
#include <cstring>

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
		int total = 0;

		while(total < 131){
			int bytesRecv = recv(socket, buffer + total, 131 - total, 0);
			if(bytesRecv <= 0){
				log.err("Broken pipe");
				closesocket(socket);
				return nullptr;
			}
			total += bytesRecv;
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

	void sendLevel(SOCKET socket, int x, int y, int z){
		// PREPARE LEVEL DATA
		int totalBlocks = x * y * z;
		vector<uint8_t> levelData(4 + totalBlocks);

		levelData[0] = (totalBlocks >> 24) & 0xFF;
		levelData[1] = (totalBlocks >> 16) & 0xFF;
		levelData[2] = (totalBlocks >> 8) & 0xFF;
		levelData[3] = totalBlocks & 0xFF;

		for(int iy=0; iy < y; iy++){
			for(int iz=0; iz < z; iz++){
				for(int ix=0; ix < x; ix++){
					levelData[4 + ix + iz * x + iy * x * z] = 0x00;
				}
			}
		}

		// COMPRESS
		uLongf compressedSize = compressBound(levelData.size()) + 18;
		vector<uint8_t> compressed(compressedSize);

		z_stream zs = {};
		deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
		zs.next_in = levelData.data();
		zs.avail_in = levelData.size();
		zs.next_out = compressed.data();
		zs.avail_out = compressedSize;

		deflate(&zs, Z_FINISH);
		compressedSize = zs.total_out;
		deflateEnd(&zs);
		compressed.resize(compressedSize);

		// SEND PACKETS
		uint8_t initPacket = 0x02;
		send(socket, (char*)&initPacket, 1, 0);

		size_t offset = 0;
		size_t totalSize = compressed.size();

		while(offset < totalSize){
			char chunkPacket[1027] = {};
			size_t chunkLen = min((size_t)1024, totalSize - offset);
			uint8_t percent = (uint8_t)((offset + chunkLen) * 100 / totalSize);

			chunkPacket[0] = 0x03;
			chunkPacket[1] = (chunkLen >> 8) & 0xFF;
			chunkPacket[2] = chunkLen & 0xFF;
			memcpy(chunkPacket + 3, compressed.data() + offset, chunkLen);
			chunkPacket[1026] = (char)percent;

			send(socket, chunkPacket, 1027, 0);
			offset += chunkLen;
		}

		uint8_t finalPacket[7];
		uint16_t sx = (uint16_t)x;
		uint16_t sy = (uint16_t)y;
		uint16_t sz = (uint16_t)z;

		finalPacket[0] = 0x04;
		finalPacket[1] = (sx >> 8) & 0xFF; finalPacket[2] = sx & 0xFF;
		finalPacket[3] = (sy >> 8) & 0xFF; finalPacket[4] = sy & 0xFF;
		finalPacket[5] = (sz >> 8) & 0xFF; finalPacket[6] = sz & 0xFF;
		send(socket, (char*)finalPacket, sizeof(finalPacket), 0);
	}
};

Packet pack;

int main() {
	log.raw("ccraft2 v0.0.0");
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
		pack.sendLevel(clientSocket, 256, 64, 256);

		delete player;
	}

	return 0;
}

