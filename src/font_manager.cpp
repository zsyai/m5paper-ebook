#include "font_manager.h"
#include "core/perf_trace.h"
#include <SD.h>
#include <algorithm>

namespace {

// True if `path` opens and has at least a 4-byte header.
bool IsReadableFont(const std::string& path) {
    File file = SD.open(path.c_str());
    if (!file) return false;
    uint32_t char_count;
    bool ok = file.read(reinterpret_cast<uint8_t*>(&char_count), 4) == 4;
    file.close();
    return ok;
}

}  // namespace

std::string FontManager::resolveFontPath(const std::string& requested_path) {
    PERF_SCOPE("FontManager::resolveFontPath");
    // Fast path: the configured font is normally present, so check it before
    // touching the directory (the /fonts scan alone costs ~20 ms).
    if (!requested_path.empty() && requested_path != "builtin" &&
        IsReadableFont(requested_path)) {
        return requested_path;
    }
    if (requested_path != "/font.bin" && SD.exists("/font.bin") &&
        IsReadableFont("/font.bin")) {
        return "/font.bin";
    }

    // Fall back to the first readable .bin in /fonts.
    std::string found;
    if (SD.exists("/fonts")) {
        File dir = SD.open("/fonts");
        while (found.empty()) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
                String name = entry.name();
                if (name.endsWith(".bin")) {
                    std::string path = name.startsWith("/")
                        ? std::string(name.c_str())
                        : "/fonts/" + std::string(name.c_str());
                    if (path != requested_path && IsReadableFont(path)) {
                        found = path;
                    }
                }
            }
            entry.close();
        }
        dir.close();
    }
    return found.empty() ? "builtin" : found;
}

std::vector<std::string> FontManager::scanAvailableFonts() {
    PERF_SCOPE("FontManager::scanAvailableFonts");
    std::vector<std::string> available_fonts;
    
    if (SD.exists("/font.bin")) {
        available_fonts.push_back("/font.bin");
    }

    if (SD.exists("/fonts")) {
        File dir = SD.open("/fonts");
        while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
                String name = entry.name();
                String lower = name;
                lower.toLowerCase();
                // Body fonts: fixed-size .bin packs and scalable .ttf files.
                if (lower.endsWith(".bin") || lower.endsWith(".ttf")) {
                    if (name.startsWith("/")) {
                        available_fonts.push_back(name.c_str());
                    } else {
                        available_fonts.push_back("/fonts/" + std::string(name.c_str()));
                    }
                }
            }
            entry.close();
        }
        dir.close();
    }
    
    return available_fonts;
}

std::string FontManager::displayName(const std::string& path) {
    if (path.empty() || path == "builtin") return "内置";
    size_t last_slash = path.find_last_of('/');
    std::string name =
        (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name;
}
