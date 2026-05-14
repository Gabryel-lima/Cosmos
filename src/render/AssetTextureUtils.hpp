#pragma once

#include <glad/gl.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace cosmos::render {

namespace fs = std::filesystem;

struct GrayImage2D {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

inline fs::path ResolveAssetPath(const fs::path& relative_path) {
    std::error_code ec;
    fs::path probe = fs::current_path(ec);
    if (ec) {
        return relative_path;
    }

    const fs::path direct = probe / relative_path;
    if (fs::exists(direct, ec)) {
        return direct;
    }

    for (; !probe.empty(); ) {
        if (fs::is_regular_file(probe / "CMakeLists.txt", ec)) {
            const fs::path candidate = probe / relative_path;
            if (fs::exists(candidate, ec)) {
                return candidate;
            }
            break;
        }
        const fs::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }

    return relative_path;
}

inline bool ReadNextPgmToken(std::istream& input, std::string& token) {
    token.clear();
    char ch = '\0';
    while (input.get(ch)) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            continue;
        }
        if (ch == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        token.push_back(ch);
        break;
    }
    if (token.empty()) {
        return false;
    }
    while (input.get(ch)) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            break;
        }
        if (ch == '#') {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            break;
        }
        token.push_back(ch);
    }
    return true;
}

inline bool LoadPgmImage(const fs::path& path, GrayImage2D& image) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }

    std::string magic;
    std::string token;
    if (!ReadNextPgmToken(input, magic) || (magic != "P2" && magic != "P5")) {
        return false;
    }
    if (!ReadNextPgmToken(input, token)) {
        return false;
    }
    const int width = std::stoi(token);
    if (!ReadNextPgmToken(input, token)) {
        return false;
    }
    const int height = std::stoi(token);
    if (!ReadNextPgmToken(input, token)) {
        return false;
    }
    const int max_value = std::stoi(token);
    if (width <= 0 || height <= 0 || max_value <= 0 || max_value > 255) {
        return false;
    }

    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    if (magic == "P2") {
        for (unsigned char& pixel : image.pixels) {
            if (!ReadNextPgmToken(input, token)) {
                return false;
            }
            const int value = std::clamp(std::stoi(token), 0, max_value);
            pixel = static_cast<unsigned char>((value * 255 + max_value / 2) / max_value);
        }
        return true;
    }

    // ReadNextPgmToken already consumed the header delimiter after max_value.
    // Reading another delimiter here would consume the first raster byte.
    std::vector<unsigned char> raw(image.pixels.size());
    input.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (input.gcount() != static_cast<std::streamsize>(raw.size())) {
        return false;
    }
    if (max_value == 255) {
        image.pixels = std::move(raw);
        return true;
    }

    for (std::size_t i = 0; i < raw.size(); ++i) {
        image.pixels[i] = static_cast<unsigned char>((static_cast<int>(raw[i]) * 255 + max_value / 2) / max_value);
    }
    return true;
}

inline bool LoadPgmTexture2D(GLuint texture_id,
                             const fs::path& relative_path,
                             int& out_width,
                             int& out_height,
                             bool generate_mipmaps = true,
                             GLenum wrap_mode = GL_REPEAT) {
    GrayImage2D image;
    const fs::path resolved_path = ResolveAssetPath(relative_path);
    if (!LoadPgmImage(resolved_path, image)) {
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, texture_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, image.width, image.height, 0,
                 GL_RED, GL_UNSIGNED_BYTE, image.pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    generate_mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);
    if (generate_mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }

    out_width = image.width;
    out_height = image.height;
    return true;
}

}  // namespace cosmos::render
