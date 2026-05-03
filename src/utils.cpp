#include "utils.hpp"
#include <algorithm>

void writeMCString(char* buf, const std::string& str){
	memset(buf, ' ', 64);
	memcpy(buf, str.c_str(), std::min(str.size(), (size_t)64));
}