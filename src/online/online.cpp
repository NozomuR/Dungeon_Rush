#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include "online/online.hpp"
#include "rules/presets.hpp"
#include "vendor/json.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <future>
#include <thread>
#include <mutex>
#include <set>
#include <functional>
namespace rush::online {
namespace {
using Json = nlohmann::json;
constexpr size_t chunk_bytes = 1048576, max_artifact = 256 * chunk_bytes;
std::filesystem::path root, completed;
std::array<Board, 9> boards;
std::mutex lock;
std::string message = "", name, my_player, my_account;
bool my_moderator = false;
DiscordLink discord;
std::future<void> job;
std::atomic_bool working = false, stopping = false;
void progress(std::string s) {
    std::lock_guard guard(lock);
    message = std::move(s);
}
void check(bool ok, const char* why) {
    if (!ok) {
        throw std::runtime_error(why);
    }
}
void cancelled() {
    check(!stopping, "Request cancelled");
}
struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) {
            WinHttpCloseHandle(h);
        }
    }
    operator HINTERNET() const { return h; }
};
std::wstring wide(std::string_view s) {
    return {s.begin(), s.end()};
}
void request(const wchar_t* method, const std::string& path, const std::string& token,
             std::string_view body, size_t limit,
             const std::function<void(const char*, size_t)>& sink) {
    cancelled();
    Handle session{WinHttpOpen(L"Dusklight-Dungeon-Rush/0.12", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               nullptr, nullptr, 0)};
    check(session.h, "Cannot initialize HTTPS");
    WinHttpSetTimeouts(session, 15000, 15000, 30000, 90000);
#ifdef DUNGEON_RUSH_LOCAL_API_TEST
    Handle connection{WinHttpConnect(session, L"127.0.0.1", 18081, 0)};
#else
    Handle connection{
        WinHttpConnect(session, L"ok-nc6x.onrender.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
#endif
    check(connection.h, "Cannot connect to leaderboard");
    Handle req{WinHttpOpenRequest(connection, method, wide(path).c_str(), nullptr, nullptr,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
#ifdef DUNGEON_RUSH_LOCAL_API_TEST
                                  0
#else
                                  WINHTTP_FLAG_SECURE
#endif
                                  )};
    check(req.h, "Cannot create HTTPS request");
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    check(WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy)),
          "Cannot disable redirects");
    std::wstring headers = L"Content-Type: ";
    headers += path.find("/chunks/") != std::string::npos ? L"application/octet-stream\r\n"
                                                          : L"application/json\r\n";
    if (!token.empty()) {
        headers += L"Authorization: Bearer " + wide(token) + L"\r\n";
    }
    check(WinHttpSendRequest(req, headers.c_str(), DWORD(headers.size()),
                             body.empty() ? nullptr : const_cast<char*>(body.data()),
                             DWORD(body.size()), DWORD(body.size()), 0),
          "Leaderboard request failed (connection)");
    check(WinHttpReceiveResponse(req, nullptr), "Leaderboard request timed out; retry when ready");
    DWORD code = 0, len = sizeof(code);
    check(WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
                              &code, &len, nullptr),
          "Missing HTTP status");
    std::array<char, 65536> buffer{};
    size_t total = 0;
    std::string error;
    for (;;) {
        cancelled();
        DWORD bytes = 0;
        check(WinHttpReadData(req, buffer.data(), DWORD(buffer.size()), &bytes),
              "Download interrupted; retry");
        if (!bytes) {
            break;
        }
        total += bytes;
        check(total <= limit, "Server response exceeded size limit");
        if (code >= 200 && code < 300) {
            sink(buffer.data(), bytes);
        } else if (error.size() < 4096) {
            error.append(buffer.data(), std::min<size_t>(bytes, 4096 - error.size()));
        }
    }
    if (code < 200 || code >= 300) {
        try {
            auto j = Json::parse(error);
            if (j.contains("detail") && j["detail"].is_string()) {
                error = j["detail"].get<std::string>();
            }
        } catch (...) {
        }
        throw std::runtime_error("HTTP " + std::to_string(code) + ": " + error.substr(0, 300));
    }
}
void remember_me(const Json& me) {
    std::lock_guard guard(lock);
    my_player = me.value("player_id", "");
    my_account = me.value("account_id", "");
    my_moderator = me.value("moderator", false);
}
Json api(const wchar_t* method, const std::string& path, const std::string& token = {},
         std::string_view body = {}) {
    std::string result;
    request(method, path, token, body, 16 * chunk_bytes,
            [&](const char* p, size_t n) { result.append(p, n); });
    return result.empty() ? Json::object() : Json::parse(result);
}
bool transient(const std::string& error) {
    // Render answers 502/503/504 while the service restarts (every deploy, with a disk attached).
    for (const char* code : {"HTTP 502", "HTTP 503", "HTTP 504"}) {
        if (error.rfind(code, 0) == 0) {
            return true;
        }
    }
    return error.find("(connection)") != std::string::npos ||
           error.find("timed out") != std::string::npos ||
           error.find("interrupted") != std::string::npos;
}
// For requests the server treats as idempotent: create, status, chunk, publish.
Json retrying(const wchar_t* method, const std::string& path, const std::string& token = {},
              std::string_view body = {}) {
    for (int attempt = 1;; ++attempt) {
        try {
            return api(method, path, token, body);
        } catch (const std::runtime_error& e) {
            if (attempt >= 4 || !transient(e.what())) {
                throw;
            }
            progress("Leaderboard is busy, retrying...");
            for (int tick = 0; tick < attempt * 20; ++tick) {
                cancelled();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}
Json read_json(const std::filesystem::path& path) {
    check(std::filesystem::file_size(path) <= 16 * chunk_bytes, "Run metadata too large");
    std::ifstream in(path);
    check(bool(in), "Cannot open run");
    return Json::parse(in);
}
void write(const std::filesystem::path& path, std::string_view data) {
    auto temp = path;
    temp += ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(data.data(), data.size());
    out.close();
    check(bool(out), "Cannot save leaderboard data");
    check(
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
        "Cannot commit leaderboard data");
}
std::string hash(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE state = nullptr;
    check(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
          "SHA256 unavailable");
    struct Cleanup {
        BCRYPT_ALG_HANDLE& a;
        BCRYPT_HASH_HANDLE& h;
        ~Cleanup() {
            if (h) {
                BCryptDestroyHash(h);
            }
            BCryptCloseAlgorithmProvider(a, 0);
        }
    } cleanup{alg, state};
    check(BCryptCreateHash(alg, &state, nullptr, 0, nullptr, 0, 0) >= 0,
          "SHA256 initialization failed");
    std::ifstream in(path, std::ios::binary);
    check(bool(in), "Missing ghost recording");
    std::array<char, 65536> buffer{};
    while (in) {
        cancelled();
        in.read(buffer.data(), buffer.size());
        check(BCryptHashData(state, reinterpret_cast<PUCHAR>(buffer.data()), ULONG(in.gcount()),
                             0) >= 0,
              "SHA256 failed");
    }
    check(in.eof(), "Cannot read ghost recording");
    std::array<unsigned char, 32> digest{};
    check(BCryptFinishHash(state, digest.data(), ULONG(digest.size()), 0) >= 0, "SHA256 failed");
    std::string result;
    constexpr char hex[] = "0123456789abcdef";
    for (auto b : digest) {
        result += hex[b >> 4];
        result += hex[b & 15];
    }
    return result;
}
bool uuid(const std::string& s) {
    if (s.size() != 36) {
        return false;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') {
                return false;
            }
        } else if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) {
            return false;
        }
    }
    return true;
}
std::string identity() {
    const auto path = root / "identity.bin";
    if (std::filesystem::exists(path)) {
        check(std::filesystem::file_size(path) < 16384, "Invalid saved player identity");
        std::ifstream in(path, std::ios::binary);
        std::string encrypted((std::istreambuf_iterator<char>(in)), {});
        DATA_BLOB source{DWORD(encrypted.size()), reinterpret_cast<BYTE*>(encrypted.data())},
            plain{};
        check(CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr,
                                 CRYPTPROTECT_UI_FORBIDDEN, &plain),
              "Saved identity belongs to another Windows account");
        std::string token(reinterpret_cast<char*>(plain.pbData), plain.cbData);
        LocalFree(plain.pbData);
        return token;
    }
    auto j = api(L"POST", "/v1/players");
    const auto token = j.at("token").get<std::string>();
    DATA_BLOB source{DWORD(token.size()), reinterpret_cast<BYTE*>(const_cast<char*>(token.data()))},
        encrypted{};
    check(CryptProtectData(&source, L"Dungeon Rush leaderboard", nullptr, nullptr, nullptr,
                           CRYPTPROTECT_UI_FORBIDDEN, &encrypted),
          "Cannot protect player identity");
    std::string bytes(reinterpret_cast<char*>(encrypted.pbData), encrypted.cbData);
    LocalFree(encrypted.pbData);
    write(path, bytes);
    return token;
}
void launch(std::function<void()> fn) {
    if (working.exchange(true)) {
        return;
    }
    if (job.valid()) {
        job.get();
    }
    job = std::async(std::launch::async, [fn = std::move(fn)] {
        try {
            fn();
        } catch (const std::exception& e) {
            progress(e.what());
        }
        working = false;
    });
}
std::string avatar_file(const std::string& account) {
    if (!uuid(account)) {
        return {};
    }
    const auto path = root / "avatars" / (account + ".png");
    try {
        if (!std::filesystem::exists(path)) {
            std::string data;
            request(L"GET", "/v1/accounts/" + account + "/avatar", {}, {}, 131072,
                    [&](const char* p, size_t n) { data.append(p, n); });
            check(data.size() >= 24 && data.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0,
                  "Invalid avatar image");
            auto dimension = [&](size_t offset) {
                uint32_t n = 0;
                for (size_t i = offset; i < offset + 4; ++i) {
                    n = (n << 8) | static_cast<unsigned char>(data[i]);
                }
                return n;
            };
            check(dimension(16) > 0 && dimension(16) <= 256 && dimension(20) > 0 &&
                      dimension(20) <= 256,
                  "Avatar dimensions too large");
            std::filesystem::create_directories(path.parent_path());
            write(path, data);
        }
        return "file://" + path.generic_string();
    } catch (const std::exception&) {
        return {};
    }
}
void fetch(std::size_t dungeon, int offset, const std::string& ruleset, bool history = false) {
    if (my_player.empty() && std::filesystem::exists(root / "identity.bin")) {
        remember_me(api(L"GET", "/v1/me", identity()));
    }
    auto j = api(L"GET", std::string("/v1/leaderboards/") + kDungeons[dungeon].id +
                             "?limit=25&offset=" + std::to_string(offset) +
                             (ruleset.empty() ? "" : "&ruleset=" + ruleset) +
                             (history ? "&history=true" : ""));
    Board b;
    b.ruleset = ruleset;
    b.total = j.at("total");
    b.offset = offset;
    b.history = history;
    check(j.at("entries").size() <= 25, "Invalid leaderboard response");
    for (const auto& e : j.at("entries")) {
        Entry row{e.at("id"),      e.at("display_name"), e.at("loadout"),
                  e.at("ruleset"), e.at("elapsed_ms"),   e.at("rank")};
        row.owner = e.value("player_id", "");
        row.account = e.value("account_id", "");
        check(uuid(row.id) && valid_name(row.name) && row.ms > 0 && row.ms <= 86400000,
              "Invalid leaderboard entry");
        row.avatar = avatar_file(e.value("account_id", ""));
        b.entries.push_back(std::move(row));
    }
    std::lock_guard guard(lock);
    b.revision = boards[dungeon].revision + 1;
    boards[dungeon] = std::move(b);
}
}  // namespace
void initialize(const std::filesystem::path& data) {
    root = data / "online";
    std::filesystem::create_directories(root / "runs");
    stopping = false;
    try {
        name = read_json(root / "preferences.json").value("display_name", "");
    } catch (...) {
    }
}
void shutdown() {
    stopping = true;
    if (job.valid()) {
        job.get();
    }
}
bool busy() {
    return working;
}
std::string status() {
    std::lock_guard guard(lock);
    return message;
}
Board board(std::size_t d) {
    std::lock_guard guard(lock);
    return boards.at(d);
}
void refresh(std::size_t d, int offset, std::string ruleset, bool history) {
    if (d >= 9 || offset < 0 || offset > 100000) {
        return;
    }
    launch([=] {
        progress("Loading leaderboard...");
        fetch(d, offset, ruleset, history);
        progress("");
    });
}
bool valid_name(const std::string& s) {
    if (s.size() < 2 || s.size() > 24 || s.back() == ' ') {
        return false;
    }
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            continue;
        }
        if (i && (c == ' ' || c == '_' || c == '.' || c == '-')) {
            continue;
        }
        return false;
    }
    return true;
}
std::string display_name() {
    return name;
}
void display_name(std::string n) {
    name = std::move(n);
    try {
        write(root / "preferences.json", Json{{"display_name", name}}.dump());
    } catch (...) {
    }
}
void submit(const std::filesystem::path& file, const std::string& username) {
    if (!valid_name(username)) {
        progress("Username: 2-24 letters, numbers, spaces, underscores, dots or hyphens.");
        return;
    }
    launch([=] {
        progress("Checking replay package...");
        auto record = read_json(file);
        check(
            record.value("mode", "dungeon") == "dungeon" && record.value("gauntlet_id", "").empty(),
            "Boss and gauntlet records are kept separately on this computer");
        // Local mode tags are not fields in the deployed dungeon API schema.
        record.erase("mode");
        record.erase("gauntlet_id");
        check(record.value("status", "") == "Finished" && !record.value("ghost_truncated", true) &&
                  !record.value("route_truncated", true),
              "Only complete, untruncated runs can be submitted");
        auto filename = std::filesystem::path(record.at("ghost_file").get<std::string>());
        check(filename == filename.filename() && filename.extension() == ".tpg",
              "Invalid ghost filename");
        auto ghost = root.parent_path() / "ghosts" / filename;
        auto animation = ghost;
        animation.replace_extension(".tpa");
        std::array<std::filesystem::path, 2> files{ghost, animation};
        std::array<std::string, 2> kinds{"ghost", "animation"};
        Json body{{"client_run_id", file.stem().string()},
                  {"display_name", username},
                  {"record", record}};
        for (int k = 0; k < 2; ++k) {
            const auto size = std::filesystem::file_size(files[k]);
            check(size >= 24 && size <= max_artifact,
                  "Missing or oversized ghost animation recording");
            body[kinds[k]] = {{"size", size}, {"sha256", hash(files[k])}};
        }
        const auto token = identity();
        auto created = retrying(L"POST", "/v1/submissions", token, body.dump());
        const auto id = created.at("id").get<std::string>();
        check(uuid(id), "Invalid submission ID");
        if (!created.value("published", false)) {
            const auto received = retrying(L"GET", "/v1/submissions/" + id, token).at("received");
            for (int k = 0; k < 2; ++k) {
                std::set<int> sent = received.at(kinds[k]).get<std::set<int>>();
                std::ifstream in(files[k], std::ios::binary);
                std::string chunk(chunk_bytes, '\0');
                int number = 0;
                const auto total = std::filesystem::file_size(files[k]);
                while (in) {
                    cancelled();
                    in.read(chunk.data(), chunk.size());
                    const auto count = in.gcount();
                    if (!count) {
                        break;
                    }
                    progress("Uploading " + kinds[k] + " " +
                             std::to_string(std::min<uint64_t>((number + 1) * chunk_bytes, total) *
                                            100 / total) +
                             "%");
                    if (!sent.contains(number)) {
                        retrying(L"PUT",
                                 "/v1/submissions/" + id + "/" + kinds[k] + "/chunks/" +
                                     std::to_string(number),
                                 token, std::string_view(chunk.data(), size_t(count)));
                    }
                    ++number;
                }
                check(in.eof(), "Ghost read failed");
            }
            progress("Publishing run...");
            retrying(L"POST", "/v1/submissions/" + id + "/publish", token);
        }
        write(root / (file.stem().string() + ".receipt.json"),
              Json{{"id", id}, {"display_name", username}}.dump());
        for (size_t d = 0; d < 9; ++d) {
            if (record.at("dungeon") == kDungeons[d].id) {
                fetch(d, 0, {});
            }
        }
        progress("Run submitted. Your local recording is preserved.");
    });
}
bool moderator() {
    std::lock_guard guard(lock);
    return my_moderator;
}
bool mine(const Entry& entry) {
    std::lock_guard guard(lock);
    return (!my_player.empty() && entry.owner == my_player) ||
           (!my_account.empty() && entry.account == my_account);
}
void withdraw(const std::string& id) {
    if (!uuid(id) || busy()) {
        return;
    }
    launch([id] {
        progress("Removing run...");
        retrying(L"DELETE", "/v1/submissions/" + id, identity());
        // Refresh every board that showed the run so it disappears without a manual refresh.
        for (std::size_t d = 0; d < boards.size(); ++d) {
            const auto b = board(d);
            if (std::any_of(b.entries.begin(), b.entries.end(),
                            [&](const Entry& e) { return e.id == id; })) {
                fetch(d, b.offset, b.ruleset, b.history);
            }
        }
        progress("Run removed from the leaderboard. Your local recording is kept.");
    });
}
void download(const std::string& id) {
    if (!uuid(id)) {
        return;
    }
    launch([=] {
        progress("Downloading run details...");
        auto package = api(L"GET", "/v1/runs/" + id);
        auto record = package.at("record");
        check(record.value("schema", 0) == 2 && record.value("status", "") == "Finished",
              "Unsupported remote run");
        const auto dir = root / "runs" / id;
        std::filesystem::create_directories(dir);
        for (const auto kind : {"ghost", "animation"}) {
            const auto& artifact = package.at("artifacts").at(kind);
            const auto size = artifact.at("size").get<uint64_t>();
            const auto expected = artifact.at("sha256").get<std::string>();
            check(size >= 24 && size <= max_artifact && expected.size() == 64,
                  "Invalid replay manifest");
            auto path = dir / (std::string(kind) == "ghost" ? "ghost.tpg" : "ghost.tpa");
            if (std::filesystem::exists(path) && std::filesystem::file_size(path) == size &&
                hash(path) == expected) {
                continue;
            }
            auto temp = path;
            temp += ".part";
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            size_t downloaded = 0;
            request(L"GET", "/v1/runs/" + id + "/" + kind, {}, {}, size,
                    [&](const char* p, size_t n) {
                        out.write(p, n);
                        check(bool(out), "Cannot save downloaded ghost");
                        downloaded += n;
                        progress(std::string("Downloading ") + kind + " " +
                                 std::to_string(downloaded * 100 / size) + "%");
                    });
            out.close();
            check(downloaded == size && hash(temp) == expected,
                  "Replay checksum mismatch; retry download");
            check(MoveFileExW(temp.c_str(), path.c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
                  "Cannot commit downloaded ghost");
        }
        record["ghost_file"] = "ghost.tpg";
        record["online_name"] = package.at("display_name");
        record["online_id"] = id;
        write(dir / "run.json", record.dump());
        {
            std::lock_guard guard(lock);
            completed = dir / "run.json";
            message = "Run downloaded.";
        }
    });
}
std::filesystem::path take_download() {
    std::lock_guard guard(lock);
    auto path = completed;
    completed.clear();
    return path;
}
DiscordLink discord_link() {
    std::lock_guard guard(lock);
    return discord;
}
void discord_begin() {
    if (busy()) {
        return;
    }
    {
        std::lock_guard guard(lock);
        discord = {};
        discord.status = "starting";
    }
    launch([] {
        try {
            const auto token = identity();
            auto me = api(L"GET", "/v1/me", token);
            remember_me(me);
            if (me.value("discord_linked", false)) {
                auto avatar = avatar_file(me.value("account_id", ""));
                std::lock_guard guard(lock);
                discord.status = "linked";
                discord.username = me.at("discord_username");
                discord.avatar = std::move(avatar);
                return;
            }
            auto j = api(L"POST", "/v1/auth/discord/link", token);
            const auto id = j.at("session_id").get<std::string>();
            const auto path = j.at("browser_path").get<std::string>();
            constexpr std::string_view prefix = "/v1/auth/discord/start?ticket=";
            check(uuid(id) && path.starts_with(prefix) && path.size() < 128,
                  "Invalid Discord link response");
            for (char c : std::string_view(path).substr(prefix.size())) {
                check((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                          c == '-' || c == '_',
                      "Invalid Discord link ticket");
            }
            std::lock_guard guard(lock);
            discord = {id, "pending", "", "https://ok-nc6x.onrender.com" + path};
        } catch (const std::exception& e) {
            std::lock_guard guard(lock);
            discord.status = "failed";
            message = e.what();
        }
    });
}
void discord_poll() {
    const auto current = discord_link();
    if (current.session.empty() || busy()) {
        return;
    }
    launch([id = current.session] {
        try {
            auto j = api(L"GET", "/v1/auth/discord/link/" + id, identity());
            std::lock_guard guard(lock);
            if (discord.session == id) {
                discord.status = j.at("status");
                discord.username = j.value("username", "");
            }
        } catch (const std::exception& e) {
            std::lock_guard guard(lock);
            discord.status = "failed";
            message = e.what();
        }
    });
}
void discord_confirm(const std::string& code) {
    const auto current = discord_link();
    if (current.status != "confirm" || busy()) {
        return;
    }
    {
        std::lock_guard guard(lock);
        discord.error.clear();
    }
    launch([id = current.session, code] {
        try {
            auto j = api(L"POST", "/v1/auth/discord/link/" + id + "/confirm", identity(),
                         Json{{"code", code}}.dump());
            auto avatar = avatar_file(j.value("account_id", ""));
            std::lock_guard guard(lock);
            discord.status = "linked";
            discord.username = j.at("discord_username");
            discord.avatar = std::move(avatar);
            message = "Discord linked. Your existing times are kept.";
            my_account = j.value("account_id", "");
        } catch (const std::exception& e) {
            // A wrong code leaves the session open; ask the server whether it still is.
            std::string status = "failed";
            try {
                status = api(L"GET", "/v1/auth/discord/link/" + id, identity())
                             .value("status", "failed");
            } catch (...) {
            }
            std::lock_guard guard(lock);
            if (discord.session == id) {
                discord.status = status;
                discord.error = e.what();
            }
            if (status != "confirm") {
                message = e.what();
            }
        }
    });
}
void discord_cancel() {
    const auto current = discord_link();
    if (current.session.empty() || current.status == "linked" || busy()) {
        return;
    }
    launch([id = current.session] {
        api(L"DELETE", "/v1/auth/discord/link/" + id, identity());
        std::lock_guard guard(lock);
        discord = {};
    });
}
void discord_refresh() {
    if (busy() || !std::filesystem::exists(root / "identity.bin")) {
        return;
    }
    launch([] {
        auto j = api(L"GET", "/v1/me", identity());
        remember_me(j);
        if (j.value("discord_linked", false)) {
            auto avatar = avatar_file(j.value("account_id", ""));
            std::lock_guard guard(lock);
            discord.status = "linked";
            discord.username = j.at("discord_username");
            discord.avatar = std::move(avatar);
        }
    });
}
void discord_sign_out() {
    if (busy()) {
        return;
    }
    launch([] {
        const auto path = root / "identity.bin";
        if (std::filesystem::exists(path)) {
            std::filesystem::create_directories(root / "saved-identities");
            const auto backup =
                root / "saved-identities" / (std::to_string(GetTickCount64()) + ".bin");
            check(MoveFileW(path.c_str(), backup.c_str()),
                  "Could not save the previous account credential");
        }
        std::lock_guard guard(lock);
        discord = {};
        my_player.clear();
        my_account.clear();
        my_moderator = false;
        message = "Signed out. Existing leaderboard runs are kept.";
    });
}

}  // namespace rush::online
