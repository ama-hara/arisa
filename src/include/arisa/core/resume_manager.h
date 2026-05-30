#pragma once

#include "arisa/core/types.h"
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

namespace arisa {

struct ChunkRecord {
    int         index;
    FileOffset  start;
    FileOffset  end;
    FileOffset  downloaded;
    bool        done;
};

struct ControlFile {
    std::string              url;
    std::string              output_path;
    FileOffset               file_size;
    int                      num_chunks;
    std::vector<ChunkRecord> chunks;

    auto save(const std::string& path) const -> bool {
        std::ofstream f(path, std::ios::trunc);
        if (!f) return false;
        f << url << "\n";
        f << output_path << "\n";
        f << file_size << "\n";
        f << num_chunks << "\n";
        for (auto& c : chunks) {
            f << c.index << " " << c.start << " " << c.end
              << " " << c.downloaded << " " << (c.done ? 1 : 0) << "\n";
        }
        return true;
    }

    static auto load(const std::string& path) -> std::optional<ControlFile> {
        std::ifstream f(path);
        if (!f) return std::nullopt;

        ControlFile cf;
        std::getline(f, cf.url);
        std::getline(f, cf.output_path);
        f >> cf.file_size >> cf.num_chunks;

        cf.chunks.resize(cf.num_chunks);
        for (int i = 0; i < cf.num_chunks; ++i) {
            int done_int;
            f >> cf.chunks[i].index >> cf.chunks[i].start
              >> cf.chunks[i].end >> cf.chunks[i].downloaded >> done_int;
            cf.chunks[i].done = (done_int != 0);
        }

        if (f.fail()) return std::nullopt;
        return cf;
    }
};

inline auto control_path_for(const std::string& file_path) -> std::string {
    return file_path + ".arisa";
}

inline auto is_resumable(const std::string& file_path) -> bool {
    return std::filesystem::exists(control_path_for(file_path));
}

} // namespace arisa