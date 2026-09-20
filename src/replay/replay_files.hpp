#pragma once
#include "vendor/json.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
namespace rush::replay_files {
namespace fs = std::filesystem;
struct Removal {
    fs::path root, record;
    std::vector<fs::path> files;
};
inline bool within(const fs::path& path, const fs::path& folder) {
    auto relative = fs::weakly_canonical(path).lexically_relative(fs::weakly_canonical(folder));
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != fs::path("..");
}
inline nlohmann::json read(const fs::path& path) {
    if (fs::file_size(path) > 32 * 1024 * 1024) {
        throw std::runtime_error("Replay metadata is too large");
    }
    std::ifstream in(path);
    return nlohmann::json::parse(in);
}
inline Removal plan(const fs::path& data, const fs::path& record) {
    Removal out{fs::weakly_canonical(data), fs::weakly_canonical(record), {}};
    const bool local = out.record.parent_path() == out.root / "runs";
    const auto downloads = out.root / "online" / "runs";
    const bool downloaded = within(out.record, downloads) &&
                            out.record.parent_path().parent_path() == downloads &&
                            out.record.filename() == "run.json";
    if ((!local && !downloaded) || out.record.extension() != ".json") {
        throw std::runtime_error("Replay is outside managed storage");
    }
    auto add = [&](const fs::path& path, const fs::path& folder) {
        if (!within(path, folder)) {
            throw std::runtime_error("Replay file points outside its storage folder");
        }
        if (fs::exists(path)) {
            if (!fs::is_regular_file(path)) {
                throw std::runtime_error("Replay path is not a file");
            }
            out.files.push_back(path);
        }
    };
    const auto j = read(out.record);
    const fs::path ghost = j.value("ghost_file", "");
    if (!ghost.empty()) {
        if (ghost != ghost.filename() || ghost.extension() != ".tpg") {
            throw std::runtime_error("Invalid ghost filename");
        }
        bool shared = false;
        if (local) {
            for (const auto& entry : fs::directory_iterator(out.root / "runs")) {
                if (entry.path().extension() == ".json" &&
                    fs::weakly_canonical(entry.path()) != out.record &&
                    read(entry.path()).value("ghost_file", "") == ghost.string()) {
                    shared = true;
                }
            }
        }
        if (!shared) {
            const auto folder = local ? out.root / "ghosts" : out.record.parent_path();
            auto file = folder / ghost;
            add(file, folder);
            file.replace_extension(".tpa");
            add(file, folder);
        }
    }
    if (local) {
        add(out.root / "online" / (out.record.stem().string() + ".receipt.json"),
            out.root / "online");
    }
    // Keep the JSON until all companion files have been removed successfully.
    out.files.push_back(out.record);
    return out;
}
inline void erase(const Removal& expected) {
    // Re-check paths and shared-file ownership after the confirmation dialog.
    auto current = plan(expected.root, expected.record);
    if (current.files != expected.files) {
        throw std::runtime_error("Replay files changed; select Delete again");
    }
    for (const auto& file : current.files) {
        if (!fs::remove(file)) {
            throw std::runtime_error("A replay file could not be removed");
        }
    }
    if (current.record.parent_path().parent_path() == current.root / "online" / "runs") {
        std::error_code ignored;
        fs::remove(current.record.parent_path(), ignored);  // Empty directory only.
    }
}
}  // namespace rush::replay_files
