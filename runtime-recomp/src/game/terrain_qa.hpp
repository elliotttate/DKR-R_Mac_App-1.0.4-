#pragma once

// Explicit, offline-only input replay for native terrain acceptance checks.
// Inactive unless DKR_TERRAIN_QA_ROUTE is set and F6 is pressed. It operates at
// the game input boundary, without synthesizing OS events or editing game RAM.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace dkr::runtime::terrain::qa {
struct Step {
    std::uint64_t end;
    std::uint16_t buttons;
    float x, y;
};
inline std::mutex mutex;
inline std::vector<Step> steps;
inline std::uint64_t start_frame = std::numeric_limits<std::uint64_t>::max();
inline unsigned route_player = 0;
inline bool virtual_port(int port) {
    const char* players = std::getenv("DKR_TERRAIN_QA_PLAYERS");
    return std::getenv("DKR_TERRAIN_QA_ROUTE") && players && players[0] >= '1' && players[0] <= '4' &&
           players[1] == '\0' && port >= 0 && port < players[0] - '0';
}
inline bool load(const char* path) {
    if (!path)
        return false;
    std::ifstream file(path);
    std::string line;
    std::vector<Step> next;
    std::uint64_t end = 0;
    unsigned player = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream row(line);
        if (line.starts_with("player ")) {
            std::string keyword;
            if (!(row >> keyword >> player) || player > 3)
                return false;
            continue;
        }
        int duration = 0;
        std::string buttons;
        float x = 0, y = 0;
        if (!(row >> duration >> buttons >> x >> y) || duration <= 0 || duration > 18000)
            return false;
        try {
            std::size_t parsed = 0;
            auto bits = std::stoul(buttons, &parsed, 0);
            if (parsed != buttons.size() || bits > 65535 || !std::isfinite(x) || !std::isfinite(y) || x < -1 || x > 1 ||
                y < -1 || y > 1 || next.size() >= 1024)
                return false;
            end += duration;
            next.push_back({end, static_cast<std::uint16_t>(bits), x, y});
        } catch (...) {
            return false;
        }
    }
    if (next.empty())
        return false;
    std::lock_guard lock(mutex);
    steps = std::move(next);
    route_player = player;
    start_frame = std::numeric_limits<std::uint64_t>::max();
    std::fprintf(stderr, "[terrain-qa] route armed: %zu steps, %llu simulation ticks\n", steps.size(),
                 static_cast<unsigned long long>(end));
    return true;
}
inline bool input(unsigned player, std::uint64_t frame, std::uint16_t* buttons, float* x, float* y) {
    std::lock_guard lock(mutex);
    if (steps.empty() || player != route_player)
        return false;
    if (start_frame == std::numeric_limits<std::uint64_t>::max())
        start_frame = frame;
    auto elapsed = frame - start_frame;
    auto step = std::find_if(steps.begin(), steps.end(), [&](const Step& s) { return elapsed < s.end; });
    if (step == steps.end()) {
        steps.clear();
        std::fprintf(stderr, "[terrain-qa] route completed\n");
        *buttons = 0;
        *x = *y = 0;
        return true;
    }
    *buttons = step->buttons;
    *x = step->x;
    *y = step->y;
    return true;
}
} // namespace dkr::runtime::terrain::qa
