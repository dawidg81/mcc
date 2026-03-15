#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN

#include <sstream>
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <zlib.h>
#include <vector>
#include <cstring>
#include <fstream>
#include <queue>

#ifndef _WIN32
#include <netdb.h>
#endif

#include "Logger.hpp"
#include "Socket.hpp"

using namespace std;

Logger logger;
string serverSalt = "472bm7";

auto writeMCString = [](char* buf, const string& str){
	memset(buf, ' ', 64);
	memcpy(buf, str.c_str(), min(str.size(), (size_t)64));
};

class Level {
public:
	int sizeX, sizeY, sizeZ;
	vector<uint8_t> blocks;

	Level(int x, int y, int z) : sizeX(x), sizeY(y), sizeZ(z){
		blocks.resize(x * y * z, 0x00);
	}

	void setBlock(int x, int y, int z, uint8_t id){
		if(x < 0 || x>= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) return;
		blocks[x + z * sizeX + y * sizeX * sizeZ] = id;
	}

	uint8_t getBlock(int x, int y, int z){
		if(x < 0 || x >= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) return 0;
		return blocks[x+z*sizeX+y*sizeX*sizeZ];
	}

	void newFile(){
		fill(blocks.begin(), blocks.end(), 0x00);
		for(int iz=0; iz < sizeZ; iz++){
			for(int ix=0; ix < sizeX; ix++){
				setBlock(ix, sizeY/2 - 2, iz, 3);
				setBlock(ix, sizeY/2 - 1, iz, 3);
				setBlock(ix, sizeY/2, iz, 2);
			}
		}
		logger.info("Generated new level to a file");
	}

	void save(const string& filename){
		ofstream file(filename, ios::binary);
		if(!file){logger.err("Failed to open level file for writing: " + filename); return;}

		for(int iy=0; iy < sizeY; iy++){
			for(int iz=0; iz < sizeZ; iz++){
				for(int ix=0; ix < sizeX; ix++){
					uint8_t id = getBlock(ix, iy, iz);
					if(id == 0x00) continue;

					uint8_t entry[7];
					entry[0] = (ix >> 8) & 0xFF; entry[1] = ix & 0xFF;
					entry[2] = (iy >> 8) & 0xFF; entry[3] = iy & 0xFF;
					entry[4] = (iz >> 8) & 0xFF; entry[5] = iz & 0xFF;
					entry[6] = id;
					file.write((char*)entry, 7);
				}
			}
		}
		file.close();
		logger.info("Level saved to " + filename);
	}

	void load(const string& filename){
		ifstream file(filename, ios::binary);
		if(!file){logger.err("Failed to open level file: " + filename); return;}

		fill(blocks.begin(), blocks.end(), 0x00);

		uint8_t entry[7];
		while(file.read((char*)entry, 7)){
			int ix = (entry[0] << 8) | entry[1];
			int iy = (entry[2] << 8) | entry[3];
			int iz = (entry[4] << 8) | entry[5];
			uint8_t id = entry[6];
			setBlock(ix, iy, iz, id);
		}
		file.close();
		logger.info("Level loaded from " + filename);
	}
};

class Player {
public:
	string username;
	string verKey;
	bool isOP;
	uint8_t id;
	SOCKET socket;

	short x, y, z;
	uint8_t yaw, pitch;

	Player(string uname, string verkey, bool op, SOCKET sock){
		username = uname;
		verKey = verkey;
		isOP = op;
		socket = sock;
		x = y = z = yaw = pitch = 0;
		id = 0;
	}

	mutex sendMutex;
	queue<vector<char>> sendQueue;
	bool disconnected = false;

	void enqueue(const char* data, int len){
		lock_guard<mutex> lock(sendMutex);
		if(disconnected) return;
		sendQueue.push(vector<char>(data, data + len));
	}

	void flushQueue(){
		lock_guard<mutex> lock(sendMutex);
		while(!sendQueue.empty()){
			auto& pkt = sendQueue.front();
			send(socket, pkt.data(), pkt.size(), 0);
			sendQueue.pop();
		}
	}

	mutex recvMutex;
	queue<uint8_t> recvQueue;

	void pushByte(uint8_t byte){
		lock_guard<mutex> lock(recvMutex);
		if(recvQueue.size() > 65536){
			disconnected = true;
			return;
		}
		recvQueue.push(byte);
	}

	bool popByte(uint8_t& out){
		lock_guard<mutex> lock(recvMutex);
		if(recvQueue.empty()) return false;
		out = recvQueue.front();
		recvQueue.pop();
		return true;
	}

	bool popExact(char* buf, int len){
		lock_guard<mutex> lock(recvMutex);
		if((int)recvQueue.size() < len) return false;
		for(int i = 0; i < len; i++){
			buf[i] = (char)recvQueue.front();
			recvQueue.pop();
		}
		return true;
	}

	int available(){
		lock_guard<mutex> lock(recvMutex);
		return (int)recvQueue.size();
	}
};

mutex playersMutex;
map<uint8_t, Player*> players;

uint8_t assignId(){
	for(uint8_t i = 0; i < 127; i++){
		if(players.find(i) == players.end()) return i;
	}
	return 255;
}

class Packet {
public:
	Player* recvPlayerId(SOCKET socket){
		char buffer[131] = {};
		int total = 0;

		while(total < 131){
			int bytesRecv = recv(socket, buffer + total, 131 - total, 0);
			if(bytesRecv <= 0){
				logger.err("Broken pipe");
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

		logger.info(username + " connected");
		return new Player(username, verKey, false, socket);
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
			logger.err("Failed to send buffer");
			closesocket(socket);
		}
	}

	void sendLevel(SOCKET socket, Level& level){
		// PREPARE LEVEL DATA
		int x = level.sizeX, y = level.sizeY, z = level.sizeZ;
		int totalBlocks = x * y * z;
		vector<uint8_t> levelData(4 + totalBlocks);

		levelData[0] = (totalBlocks >> 24) & 0xFF;
		levelData[1] = (totalBlocks >> 16) & 0xFF;
		levelData[2] = (totalBlocks >> 8) & 0xFF;
		levelData[3] = totalBlocks & 0xFF;

		memcpy(levelData.data() + 4, level.blocks.data(), totalBlocks);

		// COMPRESS
		uLongf compressedSize = compressBound(levelData.size()) + 18;
		vector<uint8_t> compressed(compressedSize);

		z_stream zs = {};
		int ret = deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
		if(ret != Z_OK){
			logger.err("Deflating failed: " + to_string(ret));
			return;
		}

		zs.next_in = levelData.data();
		zs.avail_in = (uInt)levelData.size();
		zs.next_out = compressed.data();
		zs.avail_out = (uInt)compressedSize;

		ret = deflate(&zs, Z_FINISH);
		if(ret != Z_STREAM_END){
			logger.err("Deflating did not finish: " + to_string(ret));
			deflateEnd(&zs);
			return;
		}
		compressedSize = zs.total_out;
		deflateEnd(&zs);
		compressed.resize(compressedSize);
		logger.debug("Compr. size: " + to_string(compressedSize));
		logger.debug("Chunks to send: " + to_string((compressedSize + 1023) / 1024));

		// SEND PACKETS
		uint8_t initPacket = 0x02;
		send(socket, (char*)&initPacket, 1, 0);

		size_t offset = 0;
		size_t totalSize = compressed.size();

		while(offset < totalSize){
			char chunkPacket[1028] = {};
			size_t chunkLen = min((size_t)1024, totalSize - offset);
			uint8_t percent = (uint8_t)((offset + chunkLen) * 100 / totalSize);

			chunkPacket[0] = 0x03;
			chunkPacket[1] = (chunkLen >> 8) & 0xFF;
			chunkPacket[2] = chunkLen & 0xFF;
			memcpy(chunkPacket + 3, compressed.data() + offset, chunkLen);
			chunkPacket[1027] = (char)percent;

			send(socket, chunkPacket, 1028, 0);
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

	void sendSpawnPlayer(SOCKET socket, Player* p){
		char buf[74] = {};
		buf[0] = 0x07;
		buf[1] = (int8_t)p->id;
		writeMCString(buf + 2, p->username);
		buf[66] = (p->x >> 8) & 0xFF; buf[67] = p->x & 0xFF;
		buf[68] = (p->y >> 8) & 0xFF; buf[69] = p->y & 0xFF;
		buf[70] = (p->z >> 8) & 0xFF; buf[71] = p->z & 0xFF;
		buf[72] = p->yaw;
		buf[73] = p->pitch;
		// send(socket, buf, sizeof(buf), 0);
		p->enqueue(buf, 74);
	}

	void sendDespawnPlayer(Player* p, Player* target){
		char buf[2] = {};
		buf[0] = 0x0c;
		buf[1] = (int8_t)p->id;
		// send(socket, buf, sizeof(buf), 0);
		target->enqueue(buf, 2);
	}

	void sendSetBlock(Player* p, short x, short y, short z, uint8_t block){
		char buf[8] = {};
		buf[0] = 0x06;
		buf[1] = (x >> 8) & 0xFF; buf[2] = x & 0xFF;
		buf[3] = (y >> 8) & 0xFF; buf[4] = y & 0xFF;
		buf[5] = (z >> 8) & 0xFF; buf[6] = z & 0xFF;
		buf[7] = block;
		// send(socket, buf, sizeof(buf), 0);
		p->enqueue(buf, 8);
	}

	void sendPositionUpdate(Player* p, Player* target){
		char buf[10] = {};
		buf[0] = 0x08;
		buf[1] = (int8_t)p->id;
		buf[2] = (p->x >> 8) & 0xFF; buf[3] = p->x & 0xFF;
		buf[4] = (p->y >> 8) & 0xFF; buf[5] = p->y & 0xFF;
		buf[6] = (p->z >> 8) & 0xFF; buf[7] = p->z & 0xFF;
		buf[8] = p->yaw;
		buf[9] = p->pitch;
		// send(socket, buf, sizeof(buf), 0);
		target->enqueue(buf, 10);
	}

	void sendMessage(Player* p, Player* target, const string& msg){
		char buf[66] = {};
		buf[0] = 0x0d;
		buf[1] = (int8_t)p->id;
		writeMCString(buf +2, msg);
		// send(socket, buf, sizeof(buf), 0);
		target->enqueue(buf, 66);
	}

	void sendDisconnect(Player* p, const string& reason){
		char buf[65] = {};
		buf[0] = 0x0e;
		writeMCString(buf + 1, reason);
		// send(socket, buf, sizeof(buf), 0);
		p->enqueue(buf, 65);
	}
};

Packet pack;
Level level(256, 64, 256);

bool recvExact(SOCKET socket, char* buf, int len){
	int total = 0;
	while(total < len){
		int n = recv(socket, buf + total, len - total, 0);
		if(n <= 0) return false;
		total += n;
	}
	return true;
}

void handlePlayer(SOCKET clientSocket){
	Player* player = pack.recvPlayerId(clientSocket);
	if(player == nullptr) return;

	string name = "ccraft Testing";
	string motd = "Welcome, " + player->username + "!";
	char utype = player->isOP ? 0x64 : 0x00;

	{
		lock_guard<mutex> lock(playersMutex);
		player->id = assignId();
		players[player->id] = player;
	}

	thread([player, clientSocket](){
		char buf[512];
		while(!player->disconnected){
			int n = recv(clientSocket, buf, sizeof(buf), 0);
			if(n <= 0){
				player->disconnected = true;
				break;
			}
			for(int i=0; i<n; i++) player->pushByte((uint8_t)buf[i]);
		}
	}).detach();

	thread senderThread([player](){
		while(true){
			this_thread::sleep_for(chrono::milliseconds(10));
			player->flushQueue();
			lock_guard<mutex> lock(playersMutex);
			if(players.find(player->id) == players.end()) break;
		}
	});
	senderThread.detach();

	pack.sendServerId(clientSocket, name, motd, utype);
	pack.sendLevel(clientSocket, level);

	{
	lock_guard<mutex> lock(playersMutex);
	for(auto& pair : players)
		pack.sendMessage(player, pair.second, "&e" + player->username + " joined the game");
	}

	player->x = (level.sizeX / 2) * 32;
	player->y = (level.sizeY / 2) * 32 + 51;
	player->z = (level.sizeZ / 2) * 32;
	{
		lock_guard<mutex> lock(playersMutex);
		for(auto& pair : players){
			Player* other = pair.second;
			if(other->id == player->id) continue;
			pack.sendSpawnPlayer(clientSocket, other); // all others -> player
			pack.sendSpawnPlayer(other->socket, player);
		}
	}

	{
		char buf[10] = {};
		buf[0] = 0x08;
		buf[1] = (int8_t)-1;
		buf[2] = (player->x >> 8) & 0xFF; buf[3] = player->x & 0xFF;
		buf[4] = (player->y >> 8) & 0xFF; buf[5] = player->y & 0xFF;
		buf[6] = (player->z >> 8) & 0xFF; buf[7] = player->z & 0xFF;
		buf[8] = player->yaw;
		buf[9] = player->pitch;
		send(clientSocket, buf, sizeof(buf), 0);
	}

	auto qrecvExact = [&](char* buf, int len) -> bool {
		while(!player->disconnected){
			if(player->popExact(buf, len)) return true;
			this_thread::sleep_for(chrono::milliseconds(1));
		}
		return false;
	};

	while(true){
		if(player->disconnected) goto disconnect;
		char packetId = 0;
		if(!qrecvExact(&packetId, 1)) break;

		switch((uint8_t)packetId){
			case 0x05:{ // set block
					  char buf[7] = {};
					  if(!qrecvExact(buf, 7)) goto disconnect;
					  short bx = (short)((uint8_t)buf[0] << 8 | (uint8_t)buf[1]);
					  short by = (short)((uint8_t)buf[2] << 8 | (uint8_t)buf[3]);
					  short bz = (short)((uint8_t)buf[4] << 8 | (uint8_t)buf[5]);
					  uint8_t mode = (uint8_t)buf[6];

					  char btbuf[1] = {};
					  if(!qrecvExact(btbuf, 1)) goto disconnect;
					  uint8_t blockType = (uint8_t)btbuf[0];

					  uint8_t newBlock = (mode == 0x01) ? blockType : 0x00;
					  level.setBlock(bx, by, bz, newBlock);

					  lock_guard<mutex> lock(playersMutex); 
					  for(auto& pair : players)
						  pack.sendSetBlock(player, bx, by, bz, newBlock);

					  break;
				  }
			case 0x08:{ // pos ort
					  char buf[9] = {};
					  if(!qrecvExact(buf, 9)) goto disconnect;
					  
					  player->x = (short)((uint8_t)buf[1] << 8 | (uint8_t)buf[2]);
					  player->y = (short)((uint8_t)buf[3] << 8 | (uint8_t)buf[4]);
					  player->z = (short)((uint8_t)buf[5] << 8 | (uint8_t)buf[6]);
					  player->yaw = (uint8_t)buf[7];
					  player->pitch = (uint8_t)buf[8];

					  lock_guard<mutex> lock(playersMutex);
					  for(auto& pair: players){
						  if(pair.second->id != player->id)
							  pack.sendPositionUpdate(player, pair.second);
					  }
					  break;
				  }
			case 0x0d:{ // msg
					  char buf[65] = {};
					  if(!qrecvExact(buf, 65)) goto disconnect;

					  string msg; msg.assign(buf + 1, 64);
					  msg.erase(msg.find_last_not_of(' ') + 1);

					  logger.info("<" + player->username + "> " + msg);

					  lock_guard<mutex> lock(playersMutex);
					  for(auto& pair : players)
						  pack.sendMessage(player, pair.second, "<" + player->username + "> " + msg);
					  break;
				  }
			default:
				  logger.err(player->username + " sent unknown packet 0x" + to_string((uint8_t)packetId));
				  pack.sendDisconnect(player, "Malformed data sent (0x" + to_string((uint8_t)packetId) + ")");
				  goto disconnect;
		}
	}
disconnect:
	{
		lock_guard<mutex> lock(playersMutex);
		players.erase(player->id);
		for(auto& pair : players)
			pack.sendDespawnPlayer(player, pair.second);
	}

	logger.info(player->username + " disconnected");
	{
	lock_guard<mutex> lock(playersMutex);
	for(auto& pair : players)
		pack.sendMessage(player, pair.second, "&e" + player->username + " left the game");
	}
	closesocket(clientSocket);
	delete player;
}

void saveLoop(){
	while(true){
		this_thread::sleep_for(chrono::minutes(5));
		lock_guard<mutex> lock(playersMutex);
		level.save("world.lvl");
	}
}

void heartbeat(){
	const string host = "www.classicube.net";
	const string path = "/server/heartbeat/";
	const int port = 80;

	while(true){
		size_t userCount;
		{
			lock_guard<mutex> lock(playersMutex);
			userCount = players.size();
		}
		
		string serverName = "ccraft%20Testing";
		string query =
			"name=" + serverName +
			"&port=25565" +
			"&users=" + to_string(userCount) +
			"&max=256" +
			"&salt=" + serverSalt +
			"&public=true" +
			"&software=ccraft2%20v0.1.0";

		string request =
			"GET " + path + "?" + query + " HTTP/1.0\r\n"
			"Host: " + host + "\r\n"
			"Connection: close\r\n"
			"\r\n";

struct hostent* he = gethostbyname(host.c_str());
if (!he) {
    logger.err("Heartbeat: DNS resolution failed");
    this_thread::sleep_for(chrono::minutes(1));
    continue;
}

int s = ::socket(AF_INET, SOCK_STREAM, 0);
if (s < 0) {
    logger.err("Heartbeat: socket creation failed");
    this_thread::sleep_for(chrono::minutes(1));
    continue;
}

struct sockaddr_in addr = {};
addr.sin_family = AF_INET;
addr.sin_port   = htons(port);
addr.sin_addr   = *(struct in_addr*)he->h_addr;

if (::connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    logger.err("Heartbeat: connect failed");
    ::close(s);
    this_thread::sleep_for(chrono::minutes(1));
    continue;
}	

		send(s, request.c_str(), (int)request.size(), 0);

		string response;
		char rbuf[512];
		int n;
		while((n = recv(s, rbuf, sizeof(rbuf) - 1, 0)) > 0){
			rbuf[n] = '\0';
			response += rbuf;
		}
		closesocket(s);

		auto pos = response.find("\r\n\r\n");
		if (pos != string::npos) {
			string body = response.substr(pos + 4);
			if (body.find("errors") != string::npos)
				logger.err("Heartbeat error: " + body);
			else
				logger.info("Heartbeat OK: " + body);
		}
		this_thread::sleep_for(chrono::minutes(1));
	}
}

int main(){
	logger.showDebug = true;
	logger.raw("ccraft2 v0.1.0");

	ifstream checkFile("world.lvl");
	if(checkFile.good()){
		checkFile.close();
		level.load("world.lvl");
	} else {
		checkFile.close();
		level.newFile();
		level.save("world.lvl");
	}

	thread(saveLoop).detach();
	thread(heartbeat).detach();

	Socket socket;
	socket.sockInit();
	socket.sockBind();
	socket.sockListen();

	socket.running = true;
	while(socket.running){
		SOCKET clientSocket = socket.sockAccept();
		if(clientSocket == INVALID_SOCKET) continue;

		thread(handlePlayer, clientSocket).detach();
	}

	return 0;
}

