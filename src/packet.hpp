#include <mutex>
#include <map>
#include <vector>

#pragma once

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
		//username.erase(username.find_first_of(string("\0 \t\r\n", 0, 6));
		auto upos = username.find_first_of(string("\0 \t\r\n", 5));
		if(upos != string::npos) username.erase(upos);

		string verKey; verKey.assign(buffer + 66, 64);
		//verKey.erase(verKey.find_first_of(" \t\r\n\0", 0, 5));
		auto vpos = verKey.find_first_of(string(" \t\r\n\0", 5));
		if(vpos != string::npos) verKey.erase(vpos);

		uint8_t unused = buffer[130];

		logger.info(username + " connected");
		return new Player(username, verKey, isPlayerOP(username), isPlayerBanned(username), socket);
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

	void sendSpawnPlayer(Player* p, Player* target){
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
		target->enqueue(buf, 74);
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

	void sendTeleport(Player* p, short x, short y, short z, uint8_t yaw, uint8_t pitch){
		char buf[10] = {};
		buf[0] = 0x08;
		buf[1] = (int8_t)-1;  // -1 = self
		buf[2] = (x >> 8) & 0xFF; buf[3] = x & 0xFF;
		buf[4] = (y >> 8) & 0xFF; buf[5] = y & 0xFF;
		buf[6] = (z >> 8) & 0xFF; buf[7] = z & 0xFF;
		buf[8] = yaw;
		buf[9] = pitch;
		p->enqueue(buf, 10);
	}

	void sendMessage(Player* p, Player* target, const string& msg){
	    const int MAX = 64;
	    string text = msg;

	    while(!text.empty()){
	        string line;
	        if((int)text.size() <= MAX){
	            line = text;
	            text = "";
	        } else {
	            int cut = MAX;
	            for(int i = MAX - 1; i >= 0; i--){
	                if(text[i] == ' '){
	                    cut = i;
	                    break;
	                }
	            }
	            line = text.substr(0, cut);
	            text = text.substr(cut == MAX ? cut : cut + 1);
	        }

        	char buf[66] = {};
	        buf[0] = 0x0d;
	        buf[1] = (int8_t)p->id;
	        writeMCString(buf + 2, line);
	        target->enqueue(buf, 66);
	    }
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
