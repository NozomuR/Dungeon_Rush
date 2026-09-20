#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
namespace rush::online {
struct Entry {
    std::string id, name, loadout, ruleset;
    int64_t ms = 0;
    int rank = 0;
    std::string avatar, owner, account;
};
struct Board {
    std::vector<Entry> entries;
    int total = 0, offset = 0;
    uint64_t revision = 0;
    std::string ruleset;
    bool history = false;
};
void initialize(const std::filesystem::path& data);
void shutdown();
bool busy();
std::string status();
Board board(std::size_t dungeon);
void refresh(std::size_t dungeon, int offset = 0, std::string ruleset = {}, bool history = false);
void submit(const std::filesystem::path& record, const std::string& name);
void download(const std::string& id);
void withdraw(const std::string& id);  // delete own run from the public leaderboard
bool mine(const Entry& entry);         // submitted by this installation or the same Discord account
bool moderator();                      // this account may delete anyone's run
std::filesystem::path take_download();
std::string display_name();
void display_name(std::string name);
bool valid_name(const std::string& name);
struct DiscordLink {
    std::string session, status, username, browser_url, avatar, error;
};
DiscordLink discord_link();
void discord_begin();
void discord_poll();
void discord_confirm(const std::string& code);
void discord_cancel();
void discord_refresh();
void discord_sign_out();
}  // namespace rush::online
