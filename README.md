# ccraft2

ccraft2 is a Minecraft Classic server software
using Classic Server Protocol.

It's still in development. Any issues can be
reported in the Issues page of GitHub repository site.

# Build and run

To build this software, run this command at the
project root:

```bash
make
```

It should create an executable called `ccraft2.exe`
(even when on Linux). To run it, simply do:
```bash
./ccraft2.exe
```
Notice that on Windows, when using PowerShell or
CMD you should use the `\` (backward slash) instead
of `/`. Although if you build it on Windows you most
likely may need to use a custom environment (MSYS2,
Cygwin, environment equipped with bash) since the
program uses POSIX standard for methods for signals, networking, etc.

# Usage

ccraft2 server software is optimized to run with
multiple clients. You can connect to it with the
original Minecraft client on version 0.30c or
[ClassiCube](https://github.com/ClassiCube/ClassiCube)
client.

You can either play the game alone in offline mode
or online with other players.

## Joining the game

You run the server executable:
```bash
./ccraft2.exe
```
Then you join the game from ClassiCube or BetaCraft Launcher.

### ClassiCube

In ClassiCube launcher you choose **Direct Connect**, type in your
nickname, then in **IP Address:Port** field you will most likely type in
`localhost:25565`.

### Betacraft

From BetaCraft Launcher you may need to click on **Select version** button and
select **c0.30_01c** from the list, then click **OK**. After that you will just
click on the **Play** button. It will ask you for address and port. Type
`localhost:25565` then click **OK**. You should join the game successfully.

### Important

* `localhost` is your host name that you will use
if you run the server on your local network. This is the *domain name*
that points to `127.0.0.1` IP address, that will also work in replacement
with `localhost`.
* `25565` is the port that server listens to by
default. If you have set up different port, also change it when joining the game.
You most likely enter that after address, starting with a colon (`:`).

## Letting others join

If you don't feel like playing alone you may want to invite other people to
join your game by connecting to your server over network. We will start from
the easiest setup which is local network.

### Local network

Your server is being put on your local network by default. That means that
everyone can connect to it if they are connected on the same network router.

To let others connect to your server, they will need to know your local IP
address that is known only in your local network. That will not be `127.0.0.1`
or `localhost` address, which is used only on a single device. To get your
local IP address you most likely will run this command on **Linux**:
```bash
ip address show
```
and this one on **Windows** in CMD or PowerShell:
```
ipconfig
```
Look for addresses starting with `192.168.x.x`. Then share this IP with others.
They will need to put that local address instead of `127.0.0.1`/`localhost`
when launcher asks them for address and port.

### Global network

We talked about local network, where people that are connected to the same
network router can join the game together. However if you would want a person
outside of this local network to join, the local IP address like `192.168.1.31`
will not work for them. In here we will talk about global network, where people
from all over the world that are connected to The Internet can join your server.
This is where stuff gets serious, huh?

The ccraft2 server puts itself into your public IP by default. But it won't be
accessible from the outer network if you won't forward the port. **Port
forwarding** is a network technique to make a server accessible publicly in The
Internet. That is done by your network router, that closes these ports by
default to make you safe. Not all network routers support it because of **CGNAT**.
Some routers have CGNAT, some not. If your router doesn't have CGNAT, you are
good to go with opening ports. You will most likely go into your browser and
type in `192.168.1.1` address where the router places its website. Every router
has different menu in their website so unfortunately I can't precisely tell you
where to go to open the port. It could be somewhere in the ***Advanced***
section of menu. If your router has CGNAT, opening ports will be harder. **But
what port to open?** If your server listens on port 25565 then you will open
port 25565 in the router.

After opening the port your server will be accessible from your public IP
address. To get your public address you can simply go to any website that checks
it for you, eg. [](https://whatsmyip.com/). Then you show your IP address for these
people who you want to connect to the server. I repeat: **you show your IP
address**. Sounds scary, and keep it sound scary. Always be careful who you give
the address. In networking technology mostly server invulnerability to attacks
will decide if you are safe or not.

Then just like earlier people enter that address and can join your server. If
they can't, make sure the port is opened. You can check that with some
websites.

#### Important

Before you even begin making your server public, you may want to give yourself
admin abilities to the server. to do that, create a new `ops.txt` file next to
the executable, then write in your nickname. Each nickname each line. You may
also want to add `blacklist.txt` to prevent some players from joining to the
server. These both `.txt` files should be located next to the server executable.
