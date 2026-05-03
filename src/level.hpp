#pragma once
#include <list>
#include <map>
#include <mutex>
#include <vector>
#include <fstream>
#include <random>
#include <atomic>
#include "Logger.hpp"
#include "logger_instance.hpp"
#include "player.hpp"
#include "packet.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <netdb.h>
#endif

void backupLevel(const string& name, const string& path);
int getLatestBackup(const string& name);

struct BigChunkKey{
	int16_t bcx, bcz;
	bool operator<(const BigChunkKey& o) const {
		if(bcx != o.bcx) return bcx < o.bcx;
		return bcz < o.bcz;
	}
	bool operator==(const BigChunkKey& o1) const {
		if(o1.bcx == bcx)
			if(o1.bcz == bcz)
				return true;
		return false;
	}

};

struct BigChunkIndex{
	BigChunkKey key;
	uint64_t offset;
};

class Level {
public:
	int sizeX, sizeY, sizeZ;
	vector<uint8_t> blocks;
	atomic<bool> dirty{false};

	Level(int x, int y, int z) : sizeX(x), sizeY(y), sizeZ(z){
		blocks.resize(x * y * z, 0x00);
	}

	void setBlock(int x, int y, int z, uint8_t id){
		if(x < 0 || x>= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) return;
		blocks[x + z * sizeX + y * sizeX * sizeZ] = id;
		dirty = true;
	}

	uint8_t getBlock(int x, int y, int z){
		if(x < 0 || x >= sizeX || y < 0 || y >= sizeY || z < 0 || z >= sizeZ) return 0;
		return blocks[x+z*sizeX+y*sizeX*sizeZ];
	}

	void newFile(){
	    fill(blocks.begin(), blocks.end(), 0x00);
	    int groundY = sizeY / 2;
	    for(int iz = 0; iz < sizeZ; iz++){
	        for(int ix = 0; ix < sizeX; ix++){
	            setBlock(ix, groundY - 2, iz, 3); // dirt
	            setBlock(ix, groundY - 1, iz, 3); // dirt
	            setBlock(ix, groundY,     iz, 2); // grass
	        }
	    }
	    dirty = false;
	    logger.info("Generated new level to a file");
	}

	void save(const string& filename){
	    ofstream file(filename, ios::binary);
	    if(!file){logger.err("Failed to open level file for writing: " + filename); return;}

	    // Magic bytes
	    file.write("CCRMCLVL", 8);

	    // World mode: finite
	    uint8_t mode = 0x00;
	    file.write((char*)&mode, 1);

	    // World boundaries (3 shorts, big-endian)
	    uint8_t bounds[6];
	    bounds[0] = (sizeX >> 8) & 0xFF; bounds[1] = sizeX & 0xFF;
	    bounds[2] = (sizeY >> 8) & 0xFF; bounds[3] = sizeY & 0xFF;
	    bounds[4] = (sizeZ >> 8) & 0xFF; bounds[5] = sizeZ & 0xFF;
	    file.write((char*)bounds, 6);

	    // Block array: chunk-order (chunk increments x, then z, then y;
	    // blocks inside chunk increment x, then z, then y)
	    int chunksX = (sizeX + 15) / 16;
	    int chunksY = (sizeY + 15) / 16;
	    int chunksZ = (sizeZ + 15) / 16;

	    for(int cy = 0; cy < chunksY; cy++)
	    for(int cz = 0; cz < chunksZ; cz++)
	    for(int cx = 0; cx < chunksX; cx++)
	    for(int ly = 0; ly < 16; ly++)
	    for(int lz = 0; lz < 16; lz++)
	    for(int lx = 0; lx < 16; lx++){
	        int wx = cx*16 + lx;
	        int wy = cy*16 + ly;
	        int wz = cz*16 + lz;
	        uint8_t id = (wx < sizeX && wy < sizeY && wz < sizeZ) ? getBlock(wx, wy, wz) : 0x00;
	        file.write((char*)&id, 1);
	    }

	    file.close();
	    logger.info("Level saved to " + filename);
	}

	void load(const string& filename){
	    ifstream file(filename, ios::binary);
	    if(!file){logger.err("Failed to open level file: " + filename); return;}

	    // Magic bytes
	    char magic[8];
	    if(!file.read(magic, 8) || memcmp(magic, "CCRMCLVL", 8) != 0){
	        logger.err("Invalid level file (bad magic): " + filename);
	        return;
	    }

	    // World mode
	    uint8_t mode = 0;
	    file.read((char*)&mode, 1);
	    if(mode != 0x00){
	        logger.err("Unsupported world mode 0x" + to_string(mode) + " in: " + filename);
	        return;
	    }

	    // World boundaries
	    uint8_t bounds[6];
	    file.read((char*)bounds, 6);
	    sizeX = (bounds[0] << 8) | bounds[1];
	    sizeY = (bounds[2] << 8) | bounds[3];
	    sizeZ = (bounds[4] << 8) | bounds[5];
	    blocks.assign(sizeX * sizeY * sizeZ, 0x00);

	    // Block array
	    int chunksX = (sizeX + 15) / 16;
	    int chunksY = (sizeY + 15) / 16;
	    int chunksZ = (sizeZ + 15) / 16;

	    for(int cy = 0; cy < chunksY; cy++)
	    for(int cz = 0; cz < chunksZ; cz++)
	    for(int cx = 0; cx < chunksX; cx++)
	    for(int ly = 0; ly < 16; ly++)
	    for(int lz = 0; lz < 16; lz++)
	    for(int lx = 0; lx < 16; lx++){
	        uint8_t id = 0;
	        file.read((char*)&id, 1);
	        int wx = cx*16 + lx;
	        int wy = cy*16 + ly;
	        int wz = cz*16 + lz;
	        if(wx < sizeX && wy < sizeY && wz < sizeZ)
	            setBlock(wx, wy, wz, id);
	    }

	    file.close();
	    logger.info("Level loaded from " + filename);
	}
};

class InfiniteLevel {
public:
    static const int BC_X = 256;
    static const int BC_Y = 64;
    static const int BC_Z = 256;
    static const int BC_BLOCKS = BC_X * BC_Y * BC_Z; // 4,194,304

    string filePath;
    uint64_t seed;
    mutex fileMutex;

    map<BigChunkKey, uint64_t> index;       // key -> file offset of block array
    map<BigChunkKey, Level*> cache;         // loaded big-chunks
    map<BigChunkKey, bool> dirty;           // which cached chunks need saving
    list<BigChunkKey> lruOrder;             // front = most recent
    static const int MAX_CACHE = 16;

    InfiniteLevel(const string& path, uint64_t worldSeed) : filePath(path), seed(worldSeed) {}

    ~InfiniteLevel(){
        flush();
        for(auto& pair : cache) delete pair.second;
    }

    // --- File I/O helpers ---

    void writeIndex(fstream& f){
        // Index is always at byte 13 (after magic:8, mode:1, seed:8 = 17... wait:
        // magic:8 + mode:1 + seed:8 = 17, then chunkCount:4 at byte 17)
        f.seekp(17);
        uint32_t count = (uint32_t)index.size();
        uint8_t cb[4];
        cb[0] = (count >> 24) & 0xFF;
        cb[1] = (count >> 16) & 0xFF;
        cb[2] = (count >>  8) & 0xFF;
        cb[3] =  count        & 0xFF;
        f.write((char*)cb, 4);

        for(auto& pair : index){
            int16_t bcx = pair.first.bcx;
            int16_t bcz = pair.first.bcz;
            uint64_t off = pair.second;
            uint8_t entry[12];
            entry[0]  = (bcx >> 8) & 0xFF; entry[1]  = bcx & 0xFF;
            entry[2]  = (bcz >> 8) & 0xFF; entry[3]  = bcz & 0xFF;
            entry[4]  = (off >> 56) & 0xFF; entry[5]  = (off >> 48) & 0xFF;
            entry[6]  = (off >> 40) & 0xFF; entry[7]  = (off >> 32) & 0xFF;
            entry[8]  = (off >> 24) & 0xFF; entry[9]  = (off >> 16) & 0xFF;
            entry[10] = (off >>  8) & 0xFF; entry[11] =  off        & 0xFF;
            f.write((char*)entry, 12);
        }
    }

    void writeBigChunkBlocks(fstream& f, uint64_t offset, Level* lvl){
        f.seekp(offset);
        // same chunk order as finite save()
        int chunksX = (BC_X + 15) / 16;
        int chunksY = (BC_Y + 15) / 16;
        int chunksZ = (BC_Z + 15) / 16;
        vector<uint8_t> buf;
        buf.reserve(BC_BLOCKS);
        for(int cy = 0; cy < chunksY; cy++)
        for(int cz = 0; cz < chunksZ; cz++)
        for(int cx = 0; cx < chunksX; cx++)
        for(int ly = 0; ly < 16; ly++)
        for(int lz = 0; lz < 16; lz++)
        for(int lx = 0; lx < 16; lx++){
            int wx = cx*16+lx, wy = cy*16+ly, wz = cz*16+lz;
            buf.push_back((wx<BC_X && wy<BC_Y && wz<BC_Z) ? lvl->getBlock(wx,wy,wz) : 0x00);
        }
        f.write((char*)buf.data(), buf.size());
    }

    void readBigChunkBlocks(fstream& f, uint64_t offset, Level* lvl){
        f.seekg(offset);
        int chunksX = (BC_X + 15) / 16;
        int chunksY = (BC_Y + 15) / 16;
        int chunksZ = (BC_Z + 15) / 16;
        vector<uint8_t> buf(BC_BLOCKS);
        f.read((char*)buf.data(), BC_BLOCKS);
        int i = 0;
        for(int cy = 0; cy < chunksY; cy++)
        for(int cz = 0; cz < chunksZ; cz++)
        for(int cx = 0; cx < chunksX; cx++)
        for(int ly = 0; ly < 16; ly++)
        for(int lz = 0; lz < 16; lz++)
        for(int lx = 0; lx < 16; lx++, i++){
            int wx = cx*16+lx, wy = cy*16+ly, wz = cz*16+lz;
            if(wx<BC_X && wy<BC_Y && wz<BC_Z)
                lvl->setBlock(wx, wy, wz, buf[i]);
        }
    }

    // --- Create a brand new infinite world file ---

    static InfiniteLevel* createNew(const string& path, uint64_t worldSeed){
        fstream f(path, ios::out | ios::binary);
        if(!f){ logger.err("Failed to create infinite level: " + path); return nullptr; }

        f.write("CCRMCLVL", 8);
        uint8_t mode = 0xFF;
        f.write((char*)&mode, 1);

        uint8_t sb[8];
        for(int i = 0; i < 8; i++) sb[i] = (worldSeed >> (56 - i*8)) & 0xFF;
        f.write((char*)sb, 8);

        // chunk count = 0
        uint8_t zero4[4] = {0,0,0,0};
        f.write((char*)zero4, 4);
        // no index entries yet
        f.close();

        logger.info("Created infinite level: " + path);
        return new InfiniteLevel(path, worldSeed);
    }

    // --- Load existing infinite world file ---

    static InfiniteLevel* loadExisting(const string& path){
        fstream f(path, ios::in | ios::binary);
        if(!f){ logger.err("Failed to open infinite level: " + path); return nullptr; }

        char magic[8];
        f.read(magic, 8);
        if(memcmp(magic, "CCRMCLVL", 8) != 0){
            logger.err("Bad magic in: " + path); return nullptr;
        }
        uint8_t mode;
        f.read((char*)&mode, 1);
        if(mode != 0xFF){
            logger.err("Not an infinite level: " + path); return nullptr;
        }

        uint8_t sb[8];
        f.read((char*)sb, 8);
        uint64_t worldSeed = 0;
        for(int i = 0; i < 8; i++) worldSeed = (worldSeed << 8) | sb[i];

        uint8_t cb[4];
        f.read((char*)cb, 4);
        uint32_t count = ((uint32_t)cb[0]<<24)|((uint32_t)cb[1]<<16)|((uint32_t)cb[2]<<8)|cb[3];

        InfiniteLevel* lvl = new InfiniteLevel(path, worldSeed);

        for(uint32_t i = 0; i < count; i++){
            uint8_t entry[12];
            f.read((char*)entry, 12);
            int16_t bcx = (int16_t)((entry[0] << 8) | entry[1]);
            int16_t bcz = (int16_t)((entry[2] << 8) | entry[3]);
            uint64_t off = 0;
            for(int j = 4; j < 12; j++) off = (off << 8) | entry[j];
            lvl->index[{bcx, bcz}] = off;
        }
        f.close();

        logger.info("Loaded infinite level: " + path + " (" + to_string(count) + " big-chunks)");
        return lvl;
    }

    // --- Generation (flat for now, Step 2 will add noise) ---

    void generateBigChunk(Level* lvl){
        fill(lvl->blocks.begin(), lvl->blocks.end(), 0x00);
        for(int iz = 0; iz < BC_Z; iz++)
        for(int ix = 0; ix < BC_X; ix++){
            lvl->setBlock(ix, 0,  iz, 7); // bedrock
            for(int iy = 1; iy <= 27; iy++) lvl->setBlock(ix, iy, iz, 1); // stone
            lvl->setBlock(ix, 28, iz, 3); // dirt
            lvl->setBlock(ix, 29, iz, 3); // dirt
            lvl->setBlock(ix, 30, iz, 3); // dirt
            lvl->setBlock(ix, 31, iz, 2); // grass
        }
        lvl->dirty = false;
    }

    // --- Get or generate a big-chunk (main entry point) ---

    Level* getBigChunk(int16_t bcx, int16_t bcz){
        lock_guard<mutex> lock(fileMutex);
        BigChunkKey key{bcx, bcz};

        // Check cache
        auto cit = cache.find(key);
        if(cit != cache.end()){
            // Move to front of LRU
            lruOrder.remove(key);
            lruOrder.push_front(key);
            return cit->second;
        }

        // Evict if cache full
        if((int)cache.size() >= MAX_CACHE){
            BigChunkKey evict = lruOrder.back();
            lruOrder.pop_back();
            if(dirty[evict]){
                fstream f(filePath, ios::in | ios::out | ios::binary);
                writeBigChunkBlocks(f, index[evict], cache[evict]);
                dirty[evict] = false;
            }
            delete cache[evict];
            cache.erase(evict);
            dirty.erase(evict);
        }

        Level* lvl = new Level(BC_X, BC_Y, BC_Z);

        auto iit = index.find(key);
        if(iit != index.end()){
            // Load from file
            fstream f(filePath, ios::in | ios::binary);
            readBigChunkBlocks(f, iit->second, lvl);
            logger.info("Loaded big-chunk (" + to_string(bcx) + "," + to_string(bcz) + ")");
        } else {
            // Generate and append to file
            generateBigChunk(lvl);

            fstream f(filePath, ios::in | ios::out | ios::binary);
            f.seekp(0, ios::end);
            uint64_t offset = (uint64_t)f.tellp();
            writeBigChunkBlocks(f, offset, lvl);
            index[key] = offset;
            writeIndex(f);
            f.close();

            logger.info("Generated big-chunk (" + to_string(bcx) + "," + to_string(bcz) + ")");
        }

        cache[key] = lvl;
        dirty[key] = false;
        lruOrder.push_front(key);
        return lvl;
    }

    void markDirty(int16_t bcx, int16_t bcz){
        lock_guard<mutex> lock(fileMutex);
        BigChunkKey key{bcx, bcz};
        if(cache.count(key)) dirty[key] = true;
    }

    void flush(){
        lock_guard<mutex> lock(fileMutex);
        fstream f(filePath, ios::in | ios::out | ios::binary);
        if(!f){ logger.err("flush: cannot open " + filePath); return; }
        for(auto& pair : dirty){
            if(!pair.second) continue;
            auto cit = cache.find(pair.first);
            if(cit == cache.end()) continue;
            writeBigChunkBlocks(f, index[pair.first], cit->second);
            pair.second = false;
        }
        writeIndex(f);
        logger.info("Flushed infinite level: " + filePath);
    }
};

class LevelRegistry {
public:
	mutex registryMutex;
	map<string, InfiniteLevel*> infiniteLevels;

	Level* getOrLoad(const string& name, bool generate = false){
		lock_guard<mutex> lock(registryMutex);
		auto it = levels.find(name);
		if(it != levels.end()) return it->second;

		string path = "maps/" + name + ".lvl";
		ifstream check(path);
		if(check.good()){
			check.close();
			Level* lvl = new Level(256, 64, 256);
			lvl->load(path);
			levels[name] = lvl;
			logger.info("Loaded level: " + name);
			return lvl;
		}

		if(generate){
			Level* lvl = new Level(256, 64, 256);
			lvl->newFile();
			lvl->save(path);
			levels[name] = lvl;
			logger.info("Generated new level: " + name);
			return lvl;
		}

		return nullptr;
	}

	void unloadIfEmpty(const string& name){
		if(name == "main") return;
		lock_guard<mutex> lock(registryMutex);
	auto it = levels.find(name);
	if(it == levels.end()) return;
	it->second->save("maps/" + name + ".lvl");
	delete it->second;
	levels.erase(it);
	logger.info("Unloaded level: " + name);
}

	void saveAll(){
		lock_guard<mutex> lock(registryMutex);
		for(auto& pair : levels)
			pair.second->save("maps/" + pair.first + ".lvl");
	}

	vector<string> listAvailable(){
		vector<string> result;
		return result;
	}

    InfiniteLevel* getOrCreateInfinite(const string& name, uint64_t seed = 0){
        lock_guard<mutex> lock(registryMutex);
        auto it = infiniteLevels.find(name);
        if(it != infiniteLevels.end()) return it->second;

        string path = "maps/" + name + ".lvl";
        ifstream check(path);
        InfiniteLevel* il;
        if(check.good()){
            check.close();
            il = InfiniteLevel::loadExisting(path);
        } else {
            if(seed == 0){
                random_device rd;
                seed = ((uint64_t)rd() << 32) | rd();
            }
            il = InfiniteLevel::createNew(path, seed);
        }
        if(il) infiniteLevels[name] = il;
        return il;
    }

    bool isInfinite(const string& name){
        lock_guard<mutex> lock(registryMutex);
        return infiniteLevels.count(name) > 0;
    }

    void saveAllInfinite(){
        // no lock here — flush() locks internally
        for(auto& pair : infiniteLevels) pair.second->flush();
    }

	map<string, Level*> levels;
};

// util
vector<string> listLevelFiles();

void backupLevel(const string& name, const string& path);

int getLatestBackup(const string& name);

/*
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
}*/
