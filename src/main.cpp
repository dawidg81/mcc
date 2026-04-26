#include <chrono>
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
#include <random>
#include <atomic>
#include <memory>
#include <functional>
#include <signal.h>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <netdb.h>
#endif

#include "Logger.hpp"
#include "Socket.hpp"
#include "crypt.hpp"
#include "utils.hpp"
#include "conf.hpp"
#include "level.hpp"

using namespace std;

LevelRegistry levelRegistry;
Socket serverSocket;

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
// Level level(256, 64, 256);

struct commandContext {
    Player* sender;
    vector<string> args;
};

class CommandHandler {
public:
    using handlerFn = function<void(commandContext&)>;

    struct CommandMeta {
        string usage;
        string shortDesc;
        string desc;
        handlerFn fn;
        CommandMeta() {}
        CommandMeta(const string& u, const string& s, const string& d, handlerFn f)
            : usage(u), shortDesc(s), desc(d), fn(f) {}
    };

    void registerCommand(const string& name, const string& usage,
                         const string& shortDesc, const string& desc, handlerFn fn){
        commands[name] = CommandMeta(usage, shortDesc, desc, fn);
    }

    bool handle(Player* sender, const string& msg){
        if(msg.empty() || msg[0] != '/') return false;

        commandContext ctx;
        ctx.sender = sender;
        istringstream ss(msg.substr(1));
        string token;
        while(ss >> token) ctx.args.push_back(token);
        if(ctx.args.empty()) return true;

        string name = ctx.args[0];
        auto it = commands.find(name);
        if(it != commands.end()){
            it->second.fn(ctx);
        } else {
            pack.sendMessage(sender, sender, "&cUnknown `" + name + "`");
        }
        return true;
    }

    void registerHelp(){
        registerCommand("help", "/help <command|page>", "Show help",
            "Without arguments, lists all commands paginated (8 per page). "
            "Pass a page number to go to that page, or a command name to see its full description.",
            [this](commandContext& ctx){
                const int PAGE_SIZE = 8;

                if(ctx.args.size() >= 2){
                    string arg = ctx.args[1];
                    bool isNum = !arg.empty() && arg.find_first_not_of("0123456789") == string::npos;

                    if(!isNum){
                        auto it = commands.find(arg);
                        if(it == commands.end()){
                            pack.sendMessage(ctx.sender, ctx.sender, "&cUnknown command: " + arg);
                            return;
                        }
                        CommandMeta& m = it->second;
                        pack.sendMessage(ctx.sender, ctx.sender, "&e-- Help: /" + arg + " --");
                        pack.sendMessage(ctx.sender, ctx.sender, "&eUsage: " + m.usage);
                        pack.sendMessage(ctx.sender, ctx.sender, "&e" + m.desc);
                        return;
                    }

                    // fall through to pagination
                }

                int page = 1;
                if(ctx.args.size() >= 2){
                    try { page = stoi(ctx.args[1]); } catch(...) { page = 1; }
                }

                vector<pair<string, CommandMeta*> > sorted;
                for(auto& p : commands) sorted.push_back(make_pair(p.first, &p.second));
                sort(sorted.begin(), sorted.end(),
                    [](const pair<string,CommandMeta*>& a, const pair<string,CommandMeta*>& b){
                        return a.first < b.first;
                    });

                int total = (int)sorted.size();
                int totalPages = max(1, (total + PAGE_SIZE - 1) / PAGE_SIZE);
                if(page < 1) page = 1;
                if(page > totalPages) page = totalPages;

                pack.sendMessage(ctx.sender, ctx.sender,
                    "&e-- Commands (page " + to_string(page) + "/" + to_string(totalPages) + ") --");

                int start = (page - 1) * PAGE_SIZE;
                int end = min(start + PAGE_SIZE, total);
                for(int i = start; i < end; i++){
                    pack.sendMessage(ctx.sender, ctx.sender,
                        "&e/" + sorted[i].first + " &f- " + sorted[i].second->shortDesc);
                }
                if(page < totalPages)
                    pack.sendMessage(ctx.sender, ctx.sender,
                        "&eType /help " + to_string(page + 1) + " for next page");
            });
    }

private:
    map<string, CommandMeta> commands;
};

CommandHandler cmdHandler;

void serverShutdown(int sig){
	logger.info("Shutting down...");
	{
		lock_guard<mutex> lock(playersMutex);
		for(auto& pair : players){
			pack.sendDisconnect(pair.second, "Game stopped");
			pair.second->flushQueue();
		}
		levelRegistry.saveAll();
	}
	logger.info("Goodbye!");
	serverSocket.sockClose();
	exit(0);
}

vector<string> listLevelFiles() {
	vector<string> result;
#ifdef _WIN32
	WIN32_FIND_DATAA fd;
	HANDLE hFind = FindFirstFileA("maps\\*.lvl", &fd);
	if (hFind == INVALID_HANDLE_VALUE) return result;
	do {
		string fname = fd.cFileName;
		result.push_back(fname.substr(0, fname.size() - 4)); // strip .lvl
	} while (FindNextFileA(hFind, &fd));
	FindClose(hFind);
#else
	DIR* dir = opendir("maps");
	if (!dir) return result;
	struct dirent* entry;
	while ((entry = readdir(dir)) != nullptr) {
		string fname = entry->d_name;
		if (fname.size() > 4 && fname.substr(fname.size() - 4) == ".lvl")
			result.push_back(fname.substr(0, fname.size() - 4));
	}
	closedir(dir);
#endif
	return result;
}

void backupLevel(const string& name, const string& path){
	string backupDir = "maps/backups/" + name + ".lvl.d";
#ifdef _WIN32
	CreateDirectoryA("maps/backups", nullptr);
	CreateDirectoryA(backupDir.c_str(), nullptr);
#else
	mkdir("maps/backups", 0755);
	mkdir(backupDir.c_str(), 0755);
#endif

	int next = 1;
	while(true){
		ifstream check(backupDir + "/" + to_string(next) + ".lvl");
		if(!check.good()) break;
		next++;
	}

	ifstream src(path, ios::binary);
	ofstream dst(backupDir + "/" + to_string(next) + ".lvl", ios::binary);
	if(src && dst){
		dst << src.rdbuf();
		logger.info("Backup " + to_string(next) + " created for level: " + name);
	} else {
		logger.err("Failed to create backup for level: " + name);
	}
}

int getLatestBackup(const string& name) {
	string backupDir = "maps/backups/" + name + ".lvl.d";
	int n = 0;
	while(true){
		ifstream check(backupDir + "/" + to_string(n + 1) + ".lvl");
		if(!check.good()) break;
		n++;
	}
	return n;
}

void switchWorld(Player* player, const string& targetName){
	Level* targetLevel = levelRegistry.getOrLoad(targetName);
	if(!targetLevel){
		pack.sendMessage(player, player, "&cLevel '" + targetName + "' not found!");
		return;
	}

	string oldLevel = player->currentLevel;

	// despawn player for others on old level
	{
		lock_guard<mutex> lock(playersMutex);
		for(auto& pair : players){
			Player* other = pair.second;
			if(other->id == player->id) continue;
			if(other->currentLevel == oldLevel){
				pack.sendDespawnPlayer(player, other);
				pack.sendDespawnPlayer(other, player);
			}
		}
	}

	player->currentLevel = targetName;

	pack.sendLevel(player->socket, *targetLevel);

	short spawnX = (targetLevel->sizeX / 2) * 32;
	short spawnY = (targetLevel->sizeY / 2) * 32 + 51;
	short spawnZ = (targetLevel->sizeZ / 2) * 32;
	player->x = spawnX;
	player->y = spawnY;
	player->z = spawnZ;
	pack.sendTeleport(player, spawnX, spawnY, spawnZ, 0, 0);

	// others on new level for new player, new player for them
	{
		lock_guard<mutex> lock(playersMutex);
		for(auto& pair : players){
			Player* other = pair.second;
			if(other->id == player->id) continue;
			if(other->currentLevel == targetName){
				pack.sendSpawnPlayer(other, player);
				pack.sendSpawnPlayer(player, other);
			}
		}
	}

	levelRegistry.unloadIfEmpty(oldLevel);
}

void initCommands(){
	//we structure the command in order: system name, usage string, short
	//description, long description
	cmdHandler.registerCommand(
			"kick",
			"/kick [player] <reason>",
			"Kick a player",
			"Force disconnects the given player from the server. Optionally provide a reason. OP only.",
			[](commandContext& ctx){
		if(!ctx.sender->isOP){
			pack.sendMessage(ctx.sender, ctx.sender, "&eYou're not an op!");
			return;
		}
		if(ctx.args.size() < 2){
			pack.sendMessage(ctx.sender, ctx.sender, "&eUsage: /kick [player name] <reason>");
			return;
		}
		
		string target = ctx.args[1];
		string reason = "";

		for(size_t i = 2; i < ctx.args.size(); i++){
			if(i > 2) reason += " ";
			reason += ctx.args[i];
		}

		lock_guard<mutex> lock(playersMutex);
		for(auto& pair : players){
			if(pair.second->username == target){
				if(reason.length() > 0){
					pack.sendDisconnect(pair.second, "You've been kicked. Reason: " + reason);
				} else {
					pack.sendDisconnect(pair.second, "You've been kicked");
				}
				return;
			}
		}
		pack.sendMessage(ctx.sender, ctx.sender, "&ePlayer `" + target + "` has been not found!");
	});

	cmdHandler.registerCommand(
			"shutdown",
			"/shutdown",
			"Shuts down the server",
			"Stops the game disconnecting all players. OP only",
			[](commandContext& ctx){
		if(!ctx.sender->isOP){
			pack.sendMessage(ctx.sender, ctx.sender, "&eYou're not an op!");
			return;
		}
		if(ctx.args.size() > 1){
			pack.sendMessage(ctx.sender, ctx.sender, "&eUsage: /shutdown");
			return;
		}
		serverShutdown(0);
	});

	cmdHandler.registerCommand(
			"info",
			"/info",
			"Shows info",
			"Prints software version",
			[](commandContext& ctx){
		if(ctx.args.size() > 1){
			pack.sendMessage(ctx.sender, ctx.sender, "&eUsage: /info");
			return;
		}

		pack.sendMessage(ctx.sender, ctx.sender, "&e = Server Info =");
		pack.sendMessage(ctx.sender, ctx.sender, "&eRunning ccraft2 v" + VERSION);
	});

	cmdHandler.registerCommand(
			"tp",
			"/tp [player]",
			"Teleport to player",
			"Teleports you to given player",
			[](commandContext& ctx){
		if(ctx.args.size() < 2){
			pack.sendMessage(ctx.sender, ctx.sender, "&eUsage: /tp [player]");
			return;
		}
		string targetName = ctx.args[1];
		lock_guard<mutex> lock(playersMutex);
		for(auto& pair : players){
			if(pair.second->username == targetName){
				Player* target = pair.second;
				pack.sendTeleport(ctx.sender, target->x, target->y, target->z, target->yaw, target->pitch);
				pack.sendMessage(ctx.sender, ctx.sender, "&eTeleported to " + targetName);
				return;
			}
		}
		pack.sendMessage(ctx.sender, ctx.sender, "&cPlayer `" + targetName + "` not found!");
	});

	cmdHandler.registerCommand(
			"save",
			"/save",
			"Saves level",
			"Saves current level to a file",
			[](commandContext& ctx){
		if(!ctx.sender->isOP){
			pack.sendMessage(ctx.sender, ctx.sender, "&eYou're not an op!");
			return;
		}
		if(ctx.args.size() > 1){
			pack.sendMessage(ctx.sender, ctx.sender, "&eUsage: /save");
			return;
		}
		Level* lvl = levelRegistry.getOrLoad(ctx.sender->currentLevel);
		if(lvl){
			string path = "maps/" + ctx.sender->currentLevel + ".lvl";
			lvl->save(path);
			pack.sendMessage(ctx.sender, ctx.sender, "&eLevel saved to " + path);
		}
	});

	cmdHandler.registerCommand(
			"join",
			"/join [level name]",
			"Join a world",
			"Loads given level",
			[](commandContext& ctx){
	if (ctx.args.size() < 2) {
		pack.sendMessage(ctx.sender, ctx.sender, "&eUsage: /join [level name]");
		return;
	}
	string targetName = ctx.args[1];
	if (targetName == ctx.sender->currentLevel) {
		pack.sendMessage(ctx.sender, ctx.sender, "&eYou are already on that level!");
		return;
	}
	switchWorld(ctx.sender, targetName);

	{
		lock_guard<mutex> lock(playersMutex);
		for (auto& pair : players) {
			pack.sendMessage(pair.second, pair.second, "&e" + ctx.sender->username + " went to &b" + targetName);
		}
	}
});

cmdHandler.registerCommand(
		"main",
		"/main",
		"Go to main level",
		"Sends you back to main level",
		[](commandContext& ctx){
	if (ctx.sender->currentLevel == "main") {
		pack.sendMessage(ctx.sender, ctx.sender, "&eYou are already on the main level!");
		return;
	}
	switchWorld(ctx.sender, "main");

	{
		lock_guard<mutex> lock(playersMutex);
		for (auto& pair : players) {
			pack.sendMessage(pair.second, pair.second, "&e" + ctx.sender->username + " went to &bmain level");
		}
	}

});

cmdHandler.registerCommand(
		"new",
		"/new [level name]",
		"Create new world",
		"Creates new world and world file with given name. OP only.",
		[](commandContext& ctx){
	if (!ctx.sender->isOP) {
		pack.sendMessage(ctx.sender, ctx.sender, "&eYou're not an op!");
		return;
	}
	if (ctx.args.size() < 2) {
		pack.sendMessage(ctx.sender, ctx.sender, "&eUsage: /new [level name]");
		return;
	}
	string name = ctx.args[1];
	string path = "maps/" + name + ".lvl";

	ifstream check(path);
	if (check.good()) {
		check.close();
		pack.sendMessage(ctx.sender, ctx.sender, "&cLevel '" + name + "' already exists!");
		return;
	}

	Level* lvl = levelRegistry.getOrLoad(name, true); // generate = true
	if (lvl)
		pack.sendMessage(ctx.sender, ctx.sender, "&eLevel '" + name + "' created!");
	else
		pack.sendMessage(ctx.sender, ctx.sender, "&cFailed to create level '" + name + "'");
});

cmdHandler.registerCommand(
		"del",
		"/del [level name]",
		"Delete a world",
		"Deletes a world with world file with given name. Backup files remain. OP only",
		[](commandContext& ctx){
	if (!ctx.sender->isOP) {
		pack.sendMessage(ctx.sender, ctx.sender, "&eYou're not an op!");
		return;
	}
	if (ctx.args.size() < 2) {
		pack.sendMessage(ctx.sender, ctx.sender, "&eUsage: /del [level name]");
		return;
	}
	string name = ctx.args[1];
	if (name == "main") {
		pack.sendMessage(ctx.sender, ctx.sender, "&cCannot delete the main level!");
		return;
	}

	// Kick all players on that level to main first
	{
		lock_guard<mutex> lock(playersMutex);
		for (auto& pair : players) {
			if (pair.second->currentLevel == name) {
				pack.sendMessage(pair.second, pair.second, "&cThe level you were on was deleted. Sending you to main.");
			}
		}
	}
	// switchWorld must be called without playersMutex held
	vector<Player*> toMove;
	{
		lock_guard<mutex> lock(playersMutex);
		for (auto& pair : players)
			if (pair.second->currentLevel == name)
				toMove.push_back(pair.second);
	}
	for (Player* p : toMove)
		switchWorld(p, "main");

	// Unload from registry if loaded
	{
		lock_guard<mutex> lock(levelRegistry.registryMutex);
		auto it = levelRegistry.levels.find(name);
		if (it != levelRegistry.levels.end()) {
			delete it->second;
			levelRegistry.levels.erase(it);
		}
	}

	// Delete the file
	string path = "maps/" + name + ".lvl";
	if (remove(path.c_str()) == 0)
		pack.sendMessage(ctx.sender, ctx.sender, "&eLevel '" + name + "' deleted.");
	else
		pack.sendMessage(ctx.sender, ctx.sender, "&cFailed to delete level file '" + name + "'");
});

cmdHandler.registerCommand(
		"wlist",
		"/wlist",
		"Lists worlds",
		"Gives a list of available worlds to join",
		[](commandContext& ctx){
	vector<string> worlds = listLevelFiles();
	if (worlds.empty()) {
		pack.sendMessage(ctx.sender, ctx.sender, "&eNo levels found in maps/");
		return;
	}
	pack.sendMessage(ctx.sender, ctx.sender, "&e = Available Levels =");
	for (const string& w : worlds) {
		string line = "&e  " + w;
		if (w == ctx.sender->currentLevel) line += " &a(you are here)";
		pack.sendMessage(ctx.sender, ctx.sender, line);
	}
});

// BACKUP CMDS START

cmdHandler.registerCommand(
		"backtp",
		"/backtp [level name] <backup number>",
		"Browse through backup files",
		"Brings player to a backup file with given number from given level. If backup number not given, uses last backup file. At least level name needed.",
		[](commandContext& ctx){
    string levelName = ctx.sender->currentLevel;
    int backupNum = -1; // -1 = latest

    if(ctx.args.size() >= 2) levelName = ctx.args[1];
    if(ctx.args.size() >= 3) {
        try { backupNum = stoi(ctx.args[2]); }
        catch(...) {
            pack.sendMessage(ctx.sender, ctx.sender, "&cInvalid backup number!");
            return;
        }
    }

    if(backupNum == -1) backupNum = getLatestBackup(levelName);
    if(backupNum == 0) {
        pack.sendMessage(ctx.sender, ctx.sender, "&cNo backups found for level '" + levelName + "'");
        return;
    }

    string backupPath = "maps/backups/" + levelName + ".lvl.d/" + to_string(backupNum) + ".lvl";
    ifstream check(backupPath);
    if(!check.good()) {
        pack.sendMessage(ctx.sender, ctx.sender, "&cBackup " + to_string(backupNum) + " not found for level '" + levelName + "'");
        return;
    }
    check.close();

    // load backup into a temporary level and send it to just this player
    Level tmp(256, 64, 256);
    tmp.load(backupPath);
    pack.sendLevel(ctx.sender->socket, tmp);

    short spawnX = (tmp.sizeX / 2) * 32;
    short spawnY = (tmp.sizeY / 2) * 32 + 51;
    short spawnZ = (tmp.sizeZ / 2) * 32;
    ctx.sender->x = spawnX;
    ctx.sender->y = spawnY;
    ctx.sender->z = spawnZ;
    pack.sendTeleport(ctx.sender, spawnX, spawnY, spawnZ, 0, 0);

    pack.sendMessage(ctx.sender, ctx.sender, "&eViewing backup " + to_string(backupNum) + " of '" + levelName + "' (read-only view)");
});

cmdHandler.registerCommand(
		"revert",
		"/revert <level name> <backup number>",
		"Revert world to backup",
		"Reverts world file with given level name to a backup file of given backup number. If level name and backup number not given, shows number of saved backups of current level. If backup number not given, reverts given level to last backup. Backup numbers don't change. OP only.",
		[](commandContext& ctx){
    if(!ctx.sender->isOP) {
        pack.sendMessage(ctx.sender, ctx.sender, "&eYou're not an op!");
        return;
    }

    string levelName = ctx.sender->currentLevel;
    int backupNum = -1;

    if(ctx.args.size() >= 2) levelName = ctx.args[1];
    if(ctx.args.size() >= 3) {
        try { backupNum = stoi(ctx.args[2]); }
        catch(...) {
            pack.sendMessage(ctx.sender, ctx.sender, "&cInvalid backup number!");
            return;
        }
    }

    int latest = getLatestBackup(levelName);

    // if only level name given (or no args), just print backup count
    if(ctx.args.size() <= 2 && ctx.args.size() >= 1) {
        if(latest == 0)
            pack.sendMessage(ctx.sender, ctx.sender, "&eNo backups found for level '" + levelName + "'");
        else
            pack.sendMessage(ctx.sender, ctx.sender, "&eLevel '" + levelName + "' has " + to_string(latest) + " backup(s)");
        return;
    }

    if(backupNum == -1) backupNum = latest;
    if(backupNum == 0) {
        pack.sendMessage(ctx.sender, ctx.sender, "&cNo backups found for level '" + levelName + "'");
        return;
    }

    string backupPath = "maps/backups/" + levelName + ".lvl.d/" + to_string(backupNum) + ".lvl";
    ifstream check(backupPath);
    if(!check.good()) {
        pack.sendMessage(ctx.sender, ctx.sender, "&cBackup " + to_string(backupNum) + " not found for level '" + levelName + "'");
        return;
    }
    check.close();

    Level* lvl = levelRegistry.getOrLoad(levelName);
    if(!lvl) {
        pack.sendMessage(ctx.sender, ctx.sender, "&cLevel '" + levelName + "' is not loaded!");
        return;
    }

    lvl->load(backupPath);

    // save as new version (this also creates a backup of the reverted state)
    int next = latest + 1;
    string newPath = "maps/" + levelName + ".lvl";
    lvl->save(newPath);

    // resend level to all players on it
    {
        lock_guard<mutex> lock(playersMutex);
        for(auto& pair : players) {
            if(pair.second->currentLevel == levelName) {
                pack.sendLevel(pair.second->socket, *lvl);
                short spawnX = (lvl->sizeX / 2) * 32;
                short spawnY = (lvl->sizeY / 2) * 32 + 51;
                short spawnZ = (lvl->sizeZ / 2) * 32;
                pair.second->x = spawnX;
                pair.second->y = spawnY;
                pair.second->z = spawnZ;
                pack.sendTeleport(pair.second, spawnX, spawnY, spawnZ, 0, 0);
            }
        }
    }

    pack.sendMessage(ctx.sender, ctx.sender, "&eLevel '" + levelName + "' reverted to backup " + to_string(backupNum) + " (saved as backup " + to_string(next) + ")");
});

// BACKUP CMDS END
/*
	cmdHandler.registerCommand("help", [](commandContext& ctx){
		if(ctx.args.size() > 1){
			pack.sendMessage(ctx.sender, ctx.sender, "&eUsage: /help");
			return;
		}

		pack.sendMessage(ctx.sender, ctx.sender, "&e * Help * [] = required, <> = optional");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/kick [player] - kick a player");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/shutdown - stops the game");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/info - show software info");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/tp [player] - teleport to player");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/save - saves current level");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/join [level] - join a level");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/main - return to main level");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/new [level] - create a new level (op)");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/del [level] - delete a level (op)");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/wlist - list available levels");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/backtp <level> <n> - view backup of a level");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/revert <level> <n> - revert level to backup (op)");
		pack.sendMessage(ctx.sender, ctx.sender, "&e/help - shows this help");
	});*/
	cmdHandler.registerHelp();
}

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

	if(player->verKey != md5(serverSalt + player->username)){
			char buf[65] = {};
			buf[0] = 0x0e;
			writeMCString(buf + 1, "Login failed!");
			send(clientSocket, buf, sizeof(buf), 0);
			logger.err(player->username + " failed authentication. Key was " + md5(serverSalt + player->username));
			closesocket(clientSocket);
			delete player;
			return;
	}

	if(player->isBanned){
		char buf[65] = {};
			buf[0] = 0x0e;
			writeMCString(buf + 1, "Player '" + player->username + "' is blacklisted");
			send(clientSocket, buf, sizeof(buf), 0);
			logger.err(player->username + " tried to join but is blacklisted");
			closesocket(clientSocket);
			delete player;
			return;
	}

	{
		lock_guard<mutex> lock(playersMutex);
		for(auto& pair : players){
			if(pair.second->username == player->username){
				char buf[65] = {};
				buf[0] = 0x0e;
				writeMCString(buf + 1, "Username '" + player->username + "' already taken");
				send(clientSocket, buf, sizeof(buf), 0);
				closesocket(clientSocket);
				delete player;
				return;
			}
		}
	}

	string name = "ccraft Testing";
	string motd = "Welcome, " + player->username + "!";
	char utype = player->isOP ? 0x64 : 0x00;
	auto stopSender = make_shared<atomic<bool>>(false);

	{
		lock_guard<mutex> lock(playersMutex);
		player->id = assignId();
		players[player->id] = player;
	}

	thread([player, clientSocket, stopSender](){
		char buf[512];
		while(!stopSender->load()){
			int n = recv(clientSocket, buf, sizeof(buf), 0);
			if(n <= 0){
				player->disconnected = true;
				break;
			}
			for(int i=0; i<n; i++) player->pushByte((uint8_t)buf[i]);
		}
	}).detach();

	/*
	thread senderThread([player](){
		while(true){
			this_thread::sleep_for(chrono::milliseconds(10));
			player->flushQueue();
			lock_guard<mutex> lock(playersMutex);
			if(players.find(player->id) == players.end()) break;
		}
	});
	senderThread.detach();
	*/

	thread([player, stopSender](){
		while(!stopSender->load()){
			this_thread::sleep_for(chrono::milliseconds(10));
			player->flushQueue();
		}
		player->flushQueue();
	}).detach();

	pack.sendServerId(clientSocket, name, motd, utype);
	Level* startLevel = levelRegistry.getOrLoad(player->currentLevel);
	pack.sendLevel(clientSocket, *startLevel);

	{
	lock_guard<mutex> lock(playersMutex);
	for(auto& pair : players)
		pack.sendMessage(player, pair.second, "&e" + player->username + " joined the game");
	}

	player->x = (startLevel->sizeX / 2) * 32;
	player->y = (startLevel->sizeY / 2) * 32 + 51;
	player->z = (startLevel->sizeZ / 2) * 32;

	{
		lock_guard<mutex> lock(playersMutex);
		for(auto& pair : players){
			Player* other = pair.second;
			if(other->id == player->id) continue;
			if(other->currentLevel != player->currentLevel) continue;
			pack.sendSpawnPlayer(other, player);
			pack.sendSpawnPlayer(player, other);
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
					  Level* lvl = levelRegistry.getOrLoad(player->currentLevel);
					  if(lvl) lvl->setBlock(bx, by, bz, newBlock);

					  lock_guard<mutex> lock(playersMutex);
					  for(auto& pair : players)
						  if(pair.second->currentLevel == player-> currentLevel)
							  pack.sendSetBlock(pair.second, bx, by, bz, newBlock);

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

					  if (cmdHandler.handle(player, msg)) break;

					  logger.info("<" + player->username + "> " + msg);

					  lock_guard<mutex> lock(playersMutex);
					  for(auto& pair : players)
						  pack.sendMessage(player, pair.second, player->username + ": " + msg);
					  break;
				  }
			default:
				  logger.err(player->username + " sent unknown packet 0x" + to_string((uint8_t)packetId));
				  pack.sendDisconnect(player, "Malformed data sent (0x" + to_string((uint8_t)packetId) + ")");
				  goto disconnect;
		}
	}
disconnect:
	string leftLevel = player->currentLevel;
	{
		lock_guard<mutex> lock(playersMutex);
		players.erase(player->id);
		for(auto& pair : players){
			if(pair.second->currentLevel == leftLevel)
				pack.sendDespawnPlayer(player, pair.second);
			pack.sendMessage(player, pair.second, "&e" + player->username + " left the game");
		}
	}

	levelRegistry.unloadIfEmpty(leftLevel);
	logger.info(player->username + " disconnected");
	stopSender->store(true);
	this_thread::sleep_for(chrono::milliseconds(50));
	closesocket(clientSocket);
	delete player;
}

void saveLoop(){
	while(true){
		this_thread::sleep_for(chrono::minutes(5));
		levelRegistry.saveAll();

		lock_guard<mutex> lock(levelRegistry.registryMutex);
		for(auto& pair : levelRegistry.levels){
			string path = "maps/" + pair.first + ".lvl";
			pair.second->save(path);
			if(pair.second->dirty){
				backupLevel(pair.first, path);
				pair.second->dirty = false;
			}
		}
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
			"&software=ccraft2%20v" + VERSION;

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
	closesocket(s);
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
#ifndef _WIN32
	signal(SIGPIPE, SIG_IGN);
#else
	signal(SIGFPE, SIG_IGN);
#endif
	signal(SIGINT, serverShutdown);
	signal(SIGTERM, serverShutdown);
	logger.showDebug = true;
	string splash = "ccraft2 v";
	splash.append(VERSION);
	logger.raw(splash);

#ifdef _WIN32
	CreateDirectoryA("maps", nullptr);
#else
	mkdir("maps", 0755);
#endif
	levelRegistry.getOrLoad("main", true);

	thread(saveLoop).detach();
	thread(heartbeat).detach();

	initCommands();

	// Socket socket; // is now global
	serverSocket.sockInit();
	serverSocket.sockBind();
	serverSocket.sockListen();

	serverSocket.running = true;
	while(serverSocket.running){
		SOCKET clientSocket = serverSocket.sockAccept();
		if(clientSocket == INVALID_SOCKET) continue;

		thread(handlePlayer, clientSocket).detach();
	}

	return 0;
}

