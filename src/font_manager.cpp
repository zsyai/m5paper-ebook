#include "font_manager.h"
#include <SD.h>
#include <algorithm>

std::string FontManager::resolveFontPath(const std::string& requested_path) {
    std::vector<std::string> attempt_paths;
    if (!requested_path.empty()) {
        attempt_paths.push_back(requested_path);
    }
    if (requested_path != "/font.bin") {
        attempt_paths.push_back("/font.bin");
    }
    
    // Find first bin in /fonts
    std::string first_fonts_bin = "";
    if (SD.exists("/fonts")) {
        File dir = SD.open("/fonts");
        while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) {
                String name = entry.name();
                if (name.endsWith(".bin")) {
                    if (name.startsWith("/")) {
                        first_fonts_bin = name.c_str();
                    } else {
                        first_fonts_bin = "/fonts/" + std::string(name.c_str());
                    }
                    entry.close();
                    break;
                }
            }
            entry.close();
        }
        dir.close();
    }
    if (!first_fonts_bin.empty() && std::find(attempt_paths.begin(), attempt_paths.end(), first_fonts_bin) == attempt_paths.end()) {
        attempt_paths.push_back(first_fonts_bin);
    }

    for (const auto& path : attempt_paths) {
        if (SD.exists(path.c_str())) {
            // Try to open to verify it's readable
            File file = SD.open(path.c_str());
            if (file) {
                uint32_t char_count;
                if (file.read(reinterpret_cast<uint8_t*>(&char_count), 4) == 4) {
                    file.close();
                    return path;
                }
                file.close();
            }
        }
    }
    
    return "builtin";
}

std::vector<std::string> FontManager::scanAvailableFonts() {
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
                if (name.endsWith(".bin")) {
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
