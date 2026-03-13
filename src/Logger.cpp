#include "Logger.hpp"

void Logger::raw(std::string msg)
{
	std::cout << msg << '\n';
}

void Logger::info(std::string msg)
{
	std::cout << "[INFO] " << msg << '\n';
}

void Logger::debug(std::string msg){
	if(showDebug){
		std::cout << "[DEBUG] " << msg << '\n';
	}
}

void Logger::err(std::string msg)
{
	std::cerr << "[ERROR] " << msg << '\n';
}
