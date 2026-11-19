// Copyright (c) 2026 dadjdjxjfjdjd-sketch. SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <fstream>

namespace iax {
// Publish only a completely closed file; failed output keeps its .partial suffix.
template<class Writer>
bool saveAtomically(std::filesystem::path const& path, Writer write) {
    auto temporary = path;
    temporary += ".partial";
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        write(out);
        out.close();
        if (!out) return false;
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        return !error;
    } catch (std::exception const&) { return false; }
}
}
