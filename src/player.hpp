#include <mutex>
#include <map>
#include <queue>
#include <mutex>
#include <map>
#include <queue>
#include <vector>
#pragma once

class Player {
public:
	string username;
	string verKey;
	bool isOP;
	bool isBanned;
	uint8_t id;
	SOCKET socket;

	string currentLevel;
	short x, y, z;
	uint8_t yaw, pitch;

	Player(string uname, string verkey, bool op, bool banned, SOCKET sock){
		username = uname;
		verKey = verkey;
		isOP = op;
		isBanned = banned;
		socket = sock;
		x = y = z = yaw = pitch = 0;
		id = 0;
		currentLevel = "main";
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

bool isPlayerOP(const string& username){
	ifstream file("ops.txt");
	if(!file) return false;
	string line;
	while(getline(file, line)){
		if(!line.empty() && line.back() == '\r') line.pop_back();
		if (line == username) return true;
	}
	return false;
}

bool isPlayerBanned(const string& username){
	ifstream file("blacklist.txt");
	if(!file) return false;
	string line;
	while(getline(file, line)){
		if(!line.empty() && line.back() == '\r') line.pop_back();
		if (line == username) return true;
	}
	return false;
}
