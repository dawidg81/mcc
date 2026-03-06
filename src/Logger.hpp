#pragma once
#include <iostream>
#include <string>

class Logger
{
public:
	void raw(std::string msg);
	void info(std::string msg);
	void err(std::string msg);
};
