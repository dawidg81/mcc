#include <map>
#include <mutex>
#include "player.hpp"
#include "packet.hpp"
#pragma once

struct commandContext {
    Player* sender;
    vector<string> args;
};

class CommandHandler {
public:
    using handlerFn = function<void(commandContext&)>;

    struct CommandMeta {
        string usage;
        string shortDesc;
        string desc;
        handlerFn fn;
        CommandMeta() {}
        CommandMeta(const string& u, const string& s, const string& d, handlerFn f)
            : usage(u), shortDesc(s), desc(d), fn(f) {}
    };

    void registerCommand(const string& name, const string& usage,
                         const string& shortDesc, const string& desc, handlerFn fn){
        commands[name] = CommandMeta(usage, shortDesc, desc, fn);
    }

    bool handle(Player* sender, const string& msg){
        if(msg.empty() || msg[0] != '/') return false;

        commandContext ctx;
        ctx.sender = sender;
        istringstream ss(msg.substr(1));
        string token;
        while(ss >> token) ctx.args.push_back(token);
        if(ctx.args.empty()) return true;

        string name = ctx.args[0];
        auto it = commands.find(name);
        if(it != commands.end()){
            it->second.fn(ctx);
        } else {
            pack.sendMessage(sender, sender, "&cUnknown command `" + name + "`");
        }
        return true;
    }

    void registerHelp(){
        registerCommand("help", "/help <command|page>", "Show help",
            "Without arguments, lists all commands paginated (8 per page). "
            "Pass a page number to go to that page, or a command name to see its full description.",
            [this](commandContext& ctx){
                const int PAGE_SIZE = 8;

                if(ctx.args.size() >= 2){
                    string arg = ctx.args[1];
                    bool isNum = !arg.empty() && arg.find_first_not_of("0123456789") == string::npos;

                    if(!isNum){
                        auto it = commands.find(arg);
                        if(it == commands.end()){
                            pack.sendMessage(ctx.sender, ctx.sender, "&cUnknown command: " + arg);
                            return;
                        }
                        CommandMeta& m = it->second;
                        pack.sendMessage(ctx.sender, ctx.sender, "&e-- Help: /" + arg + " --");
                        pack.sendMessage(ctx.sender, ctx.sender, "&eUsage: " + m.usage);
                        pack.sendMessage(ctx.sender, ctx.sender, "&e" + m.desc);
                        return;
                    }

                    // fall through to pagination
                }

                int page = 1;
                if(ctx.args.size() >= 2){
                    try { page = stoi(ctx.args[1]); } catch(...) { page = 1; }
                }

                vector<pair<string, CommandMeta*> > sorted;
                for(auto& p : commands) sorted.push_back(make_pair(p.first, &p.second));
                sort(sorted.begin(), sorted.end(),
                    [](const pair<string,CommandMeta*>& a, const pair<string,CommandMeta*>& b){
                        return a.first < b.first;
                    });

                int total = (int)sorted.size();
                int totalPages = max(1, (total + PAGE_SIZE - 1) / PAGE_SIZE);
                if(page < 1) page = 1;
                if(page > totalPages) page = totalPages;

                pack.sendMessage(ctx.sender, ctx.sender,
                    "&e-- Commands (page " + to_string(page) + "/" + to_string(totalPages) + ") --");

                int start = (page - 1) * PAGE_SIZE;
                int end = min(start + PAGE_SIZE, total);
                for(int i = start; i < end; i++){
                    pack.sendMessage(ctx.sender, ctx.sender,
                        "&e/" + sorted[i].first + " &f- " + sorted[i].second->shortDesc);
                }
                if(page < totalPages)
                    pack.sendMessage(ctx.sender, ctx.sender,
                        "&eType /help " + to_string(page + 1) + " for next page");
            });
    }

private:
    map<string, CommandMeta> commands;
};
