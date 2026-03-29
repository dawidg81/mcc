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

Your server