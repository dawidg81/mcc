#include <iostream>
#include <winsock.h>

using namespace std;

class Socket {
public:
  void winInit() { cout << "Initializing socket\n"; }
}

int main(int argc, char *argv[]) {
  cout << "mcc v0.0.0\n";

  return 0;
}
