#pragma once
#include <string>
#include <vector>

class FontManager {
public:
    // Resolves the actual font path to use, applying fallback rules.
    // Returns "builtin" if all file attempts fail.
    static std::string resolveFontPath(const std::string& requested_path);
    
    // Scans for available font files on SD card.
    static std::vector<std::string> scanAvailableFonts();

    // "内置" for the built-in font, else the file name without extension.
    static std::string displayName(const std::string& path);
};
