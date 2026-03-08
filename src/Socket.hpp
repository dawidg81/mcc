#pragma once
#include <winsock2.h>
#include "Logger.hpp"

#define NET_SOCK_ADDR "0.0.0.0"
#define NET_SOCK_PORT 25565

class Socket
{
private:
	Logger log;
	SOCKET mainSocket = INVALID_SOCKET;

public:
	int pInit();
	int pBind();
	int pListen();
	int pAccept();

	int winInit();
	int winBind();
	int winListen();
	int winAccept();
};
