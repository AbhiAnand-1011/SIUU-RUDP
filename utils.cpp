#include "utils.h"
#include "protocol.h"

#include <fstream>
#include <sys/stat.h>
#include <chrono>
#include <thread>
#include <filesystem>

std::vector<std::vector<uint8_t>> readFileChunks(const std::string &path, size_t chunk_size) {
    std::vector<std::vector<uint8_t>> chunks;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return chunks;
    while (true) {
        std::vector<uint8_t> buf;
        buf.resize(chunk_size);
        ifs.read(reinterpret_cast<char *>(buf.data()), chunk_size);
        std::streamsize r = ifs.gcount();
        if (r <= 0) break;
        buf.resize(static_cast<size_t>(r));
        chunks.push_back(std::move(buf));
    }
    return chunks;
}

bool writeChunksToFile(const std::string &outpath, const std::vector<std::vector<uint8_t>> &chunks) {
    std::filesystem::path p(outpath);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(outpath, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    for (const auto &c : chunks) {
        ofs.write(reinterpret_cast<const char *>(c.data()), c.size());
        if (!ofs) return false;
    }
    return true;
}

bool writeChunkToTemp(const std::string &outdir, uint32_t seq, const std::vector<uint8_t> &chunk) {
    std::filesystem::create_directories(outdir);
    std::string file = outdir + "/" + std::to_string(seq) + ".part";
    std::ofstream ofs(file, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs.write(reinterpret_cast<const char *>(chunk.data()), chunk.size());
    return static_cast<bool>(ofs);
}

bool assembleChunksFromDir(const std::string &outdir, const std::string &final_path, uint32_t total_chunks) {
    std::filesystem::path p(final_path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(final_path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    for (uint32_t i = 0; i < total_chunks; ++i) {
        std::string part = outdir + "/" + std::to_string(i) + ".part";
        std::ifstream ifs(part, std::ios::binary);
        if (!ifs) return false;
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ofs.write(reinterpret_cast<const char *>(buf.data()), buf.size());
        if (!ofs) return false;
    }
    return true;
}

uint64_t currentTimeMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void sleepMs(uint64_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool ensureDir(const std::string &path) {
    std::error_code ec;
    if (path.empty()) return true;
    return std::filesystem::create_directories(path) || std::filesystem::exists(path);
}
