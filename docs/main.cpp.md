# main.cpp

Documenting this file is surely long and precise, as this is where whole server logic happens, skipping sockets and logging that are in other source files.

We will take a look at the code, maybe even line by line. Let's start.

## The beginning

At the beginning of our main.cpp file we have a lot of includes, defines and `ifndef` statement. Here's how it looks like:

```cpp
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

#ifndef _WIN32
#include <netdb.h>
#endif

#include "Logger.hpp"
#include "Socket.hpp"

using namespace std;

#define VERSION "0.2.6"
```

It takes approx. 30 lines. Firstly, we have an includement of `<chrono>`. It gives us useful functions for waiting between tasks, for example proper threads stopping, so our server doesn't crash.

Then we have some defines:
```cpp
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
```
They do some Windows MinGW specific declarations. They are most likely not needed anymore, since we build everything either on MSYS2 Environment or UNIX/Linux. Safe to remove.

Then we have a lot of includes. I believe these includes can be ordered better. Firstly we include `<sstream>`. I honestly don't know what functions it give us. When I look at the header file I see a lot of `istream` and `ostream` related functions. Better if it's with us in there.

Another includement is `<thread>`. It is very important. It gives us thread-related functions so server can operate on multiple players. We have to be careful with these threads though. Along with that we have `<mutex>` that is also very important. Server integrates these both into one threading management. `mutex` allows us for blocking data of other threads so everything is happening safely.

Next include is `<map>`. It is important because it gives us a type that handles player table in the server. Very useful.

Another include is `<string>`. It gives us functions that let us truncate trailing spaces from player usernames and verification keys.

Then we have `<zlib.h>`. It let us compress the level data before we send it to the player. More about it further in the file.

Then we have `<vector>`. It gives us vectors. As useful as maps.

`<cstring>` is most likely as important as `<string>`.

`<fstream>` lets us read and write into the level file. Later we build a small level file system using functions from this header.

`<queue>` is very important because without it the server would crash. It gives us tools to queue data from player. If there is too much data received in the queue from the player, the server kicks the player. Very important.

`<random>` is with us to give useful functions for randomizing. As I remember it is used to generate server salt. Important.

`<atomic>` is as important as `<thread>`. It let multiple thread access one data.

`<memory>` lets us be more precise in operating memory. Also important.

After these includes we have an `ifndef` statement.

```cpp
#ifndef _WIN32
#include <netdb.h>
#endif
```

It tells the compiler to also include `<netdb.h>` if the program is being built on Windows. I don't remember what this header does.

After the statement we have two last includes:
```cpp
#include "Logger.hpp"
#include "Socket.hpp"
```

These point to abstraction layers for socketing and logging. More about them in other docs files.

After that we have `using namespace std;` and at the end we have version defining:

```cpp
#define VERSION "MAJOR.MINOR.PATCH"
```

It's a constant defining version of project that can be used eg. in logs and server list heartbeat. Perhaps it's not very convenient that such thing is defined in the main source file. In the future it could be moved to a Makefile.

This is the end of includes and definitions. We begin the program with creating a `logger` object of `Logger` class.

```cpp
Logger logger;
```

`Logger` class is defined in Logger.hpp header file. It's a simple abstraction layer to just print things into the console. Instead of such file we could use `std::cout`, yet `Logger` provides functions that classify logs into `INFO`, `ERROR`, and `DEBUG`. They can be useful.

After creating `logger` object we declare and define a function:

```cpp
auto writeMCString = [](char* buf, const string& str){
	memset(buf, ' ', 64);
	memcpy(buf, str.c_str(), min(str.size(), (size_t)64));
};
```

`writeMCString` is a helper function to simply write a string consisting of 64 bytes into the buffer we give to the function, then we give what string to put into the buffer.

After `writeMCString`, we implement a `generateSalt` function:

```cpp
std::string generateSalt(int bytes = 32) {
	std::random_device rd;
	std::string salt;
	salt.reserve(bytes * 2);

	for (int i = 0; i < bytes; i++) {
		unsigned char b = rd() & 0xff;
		salt += "0123456789abcdef"[b >> 4];
		salt += "0123456789abcdef"[b & 0xf];
	}
	return salt;
}
```