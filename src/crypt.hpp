#pragma once

#include <string>
#include <random>
#include <vector>

using namespace std;

std::string generateSalt(int bytes = 32);
string md5(const string& input);
extern std::string serverSalt;
