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
};
