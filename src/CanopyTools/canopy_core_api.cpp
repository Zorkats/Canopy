#include "canopy_core_api.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "game_assets.h"
#include "game_browse.h"
#include "ini_document.h"
#include "rules_project.h"
#include "rules_report.h"

// The C face of CanopyCore. Nothing may throw past it, so each call turns
// an exception into an error result.

namespace {

char* Hand(const std::string& text) {
    char* copy = static_cast<char*>(std::malloc(text.size() + 1));
    if (copy) {
        std::memcpy(copy, text.c_str(), text.size() + 1);
    }
    return copy;
}

char* Failure(const char* what) {
    return Hand(
        std::string("{\"abi\":") + std::to_string(mod_rules::kJsonAbi) + ",\"ok\":false,\"error\":\"" + what + "\"}"
    );
}

// A path from the caller, UTF-8, as the wide string CanopyCore opens files
// with: UTF-16 on Windows, UTF-32 elsewhere. Null reads as empty. A byte
// that does not start or continue a valid sequence becomes U+FFFD, which
// names no file, so a bad path fails to open rather than opening another.
std::wstring PathArg(const char* text) {
    std::wstring result;
    if (!text) {
        return result;
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
    const size_t size = std::strlen(text);
    size_t at = 0;
    while (at < size) {
        const unsigned char lead = bytes[at];
        uint32_t point = 0xFFFD;
        size_t length = 1;
        size_t extra = 0;
        uint32_t least = 0;
        if (lead < 0x80) {
            point = lead;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            extra = 1;
            point = lead & 0x1Fu;
            least = 0x80;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            extra = 2;
            point = lead & 0x0Fu;
            least = 0x800;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            extra = 3;
            point = lead & 0x07u;
            least = 0x10000;
        }
        if (extra > 0) {
            bool valid = at + extra < size;
            for (size_t i = 1; valid && i <= extra; ++i) {
                valid = (bytes[at + i] & 0xC0u) == 0x80u;
                point = (point << 6) | (bytes[at + i] & 0x3Fu);
            }
            if (valid && point >= least && point <= 0x10FFFF && (point < 0xD800 || point > 0xDFFF)) {
                length = extra + 1;
            } else {
                point = 0xFFFD;
            }
        }
        at += length;
        if (sizeof(wchar_t) == 2 && point >= 0x10000) {
            point -= 0x10000;
            result += static_cast<wchar_t>(0xD800 + (point >> 10));
            result += static_cast<wchar_t>(0xDC00 + (point & 0x3FFu));
        } else {
            result += static_cast<wchar_t>(point);
        }
    }
    return result;
}

// A malloc copy of data for the caller, or null when out of memory. Never
// null for empty data: malloc(0) may return null, which reads as a failure.
unsigned char* HandBytes(const std::vector<uint8_t>& data) {
    unsigned char* copy = static_cast<unsigned char*>(std::malloc(data.empty() ? 1 : data.size()));
    if (copy && !data.empty()) {
        std::memcpy(copy, data.data(), data.size());
    }
    return copy;
}

}  // namespace

extern "C" {

int canopy_core_abi(void) {
    return mod_rules::kJsonAbi;
}

char* canopy_core_sections(void) {
    try {
        return Hand(mod_rules::SectionsJson());
    } catch (...) {
        return Failure("The section list could not be built.");
    }
}

char* canopy_core_validate(const char* gameFolder, const char* modsFolder) {
    try {
        const mod_rules::Validation validation =
            mod_rules::Validate(PathArg(gameFolder), PathArg(modsFolder));
        return Hand(mod_rules::ValidationJson(validation));
    } catch (...) {
        return Failure("The check failed unexpectedly.");
    }
}

char* canopy_core_validate_project(
    const char* gameFolder, const char* modsFolder, const char* projectFolder, const char* iniText
) {
    try {
        return Hand(mod_rules::ValidateProjectJson(
            PathArg(gameFolder), PathArg(modsFolder), PathArg(projectFolder), iniText ? iniText : ""
        ));
    } catch (...) {
        return Failure("The check failed unexpectedly.");
    }
}

char* canopy_core_game(const char* gameFolder) {
    try {
        return Hand(game_browse::GameJson(PathArg(gameFolder)));
    } catch (...) {
        return Failure("The game folder could not be checked.");
    }
}

char* canopy_core_files(const char* gameFolder) {
    try {
        return Hand(game_browse::FilesJson(PathArg(gameFolder)));
    } catch (...) {
        return Failure("The file list could not be read.");
    }
}

char* canopy_core_table(const char* gameFolder, const char* file) {
    try {
        return Hand(game_browse::TableJson(PathArg(gameFolder), file ? file : ""));
    } catch (...) {
        return Failure("The table could not be read.");
    }
}

char* canopy_core_row(const char* gameFolder, const char* file, int index) {
    try {
        if (index < 0) {
            return Hand(game_browse::FailureJson("A row index is 0 or more."));
        }
        return Hand(game_browse::RowJson(
            PathArg(gameFolder), file ? file : "", static_cast<size_t>(index)
        ));
    } catch (...) {
        return Failure("The row could not be read.");
    }
}

char* canopy_core_grid(const char* gameFolder, const char* file) {
    try {
        return Hand(game_browse::GridJson(PathArg(gameFolder), file ? file : ""));
    } catch (...) {
        return Failure("The table could not be read.");
    }
}

char* canopy_core_texture(const char* gameFolder, const char* gamePath, unsigned char** pixels) {
    if (pixels) {
        *pixels = nullptr;
    }
    try {
        if (!pixels) {
            return Hand(game_browse::FailureJson("No place for the pixels was given."));
        }
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> decoded;
        std::string error;
        if (!game_browse::Texture(
                PathArg(gameFolder), gamePath ? gamePath : "", width, height, decoded, error
            )) {
            return Hand(game_browse::FailureJson(error));
        }
        unsigned char* copy = static_cast<unsigned char*>(std::malloc(decoded.size()));
        if (!copy) {
            return Hand(game_browse::FailureJson("Out of memory for the pixels."));
        }
        std::memcpy(copy, decoded.data(), decoded.size());
        *pixels = copy;
        return Hand(
            "{\"abi\":" + std::to_string(mod_rules::kJsonAbi) + ",\"ok\":true,\"error\":null,\"width\":" +
            std::to_string(width) + ",\"height\":" + std::to_string(height) + "}"
        );
    } catch (...) {
        if (*pixels) {
            std::free(*pixels);
            *pixels = nullptr;
        }
        return Failure("The texture could not be decoded.");
    }
}

char* canopy_core_file(const char* gameFolder, const char* gamePath, unsigned char** bytes) {
    if (bytes) {
        *bytes = nullptr;
    }
    try {
        if (!bytes) {
            return Hand(game_browse::FailureJson("No place for the bytes was given."));
        }
        std::vector<uint8_t> read;
        std::string error;
        if (!game_browse::File(PathArg(gameFolder), gamePath ? gamePath : "", read, error)) {
            return Hand(game_browse::FailureJson(error));
        }
        // malloc(0) may return null, which would read as a failure.
        unsigned char* copy = static_cast<unsigned char*>(std::malloc(read.empty() ? 1 : read.size()));
        if (!copy) {
            return Hand(game_browse::FailureJson("Out of memory for the file."));
        }
        if (!read.empty()) {
            std::memcpy(copy, read.data(), read.size());
        }
        *bytes = copy;
        return Hand(
            "{\"abi\":" + std::to_string(mod_rules::kJsonAbi) + ",\"ok\":true,\"error\":null,\"size\":" +
            std::to_string(read.size()) + "}"
        );
    } catch (...) {
        if (*bytes) {
            std::free(*bytes);
            *bytes = nullptr;
        }
        return Failure("The file could not be read.");
    }
}

char* canopy_core_icon(const char* gameFolder, unsigned int iconId, unsigned char** pixels) {
    if (pixels) {
        *pixels = nullptr;
    }
    try {
        if (!pixels) {
            return Hand(game_browse::FailureJson("No place for the pixels was given."));
        }
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> cut;
        std::string error;
        if (!game_assets::Icon(PathArg(gameFolder), iconId, width, height, cut, error)) {
            return Hand(game_browse::FailureJson(error));
        }
        unsigned char* copy = HandBytes(cut);
        if (!copy) {
            return Hand(game_browse::FailureJson("Out of memory for the pixels."));
        }
        *pixels = copy;
        return Hand(
            "{\"abi\":" + std::to_string(mod_rules::kJsonAbi) + ",\"ok\":true,\"error\":null,\"width\":" +
            std::to_string(width) + ",\"height\":" + std::to_string(height) + "}"
        );
    } catch (...) {
        if (*pixels) {
            std::free(*pixels);
            *pixels = nullptr;
        }
        return Failure("The icon could not be read.");
    }
}

char* canopy_core_icons(const char* gameFolder, const unsigned int* ids, int count, const char* outFolder) {
    try {
        if (count < 0 || (count > 0 && !ids)) {
            return Hand(game_browse::FailureJson("No icon ids were given."));
        }
        const std::vector<uint32_t> list(ids, ids + count);
        return Hand(game_assets::IconsJson(PathArg(gameFolder), list, PathArg(outFolder)));
    } catch (...) {
        return Failure("The icons could not be saved.");
    }
}

char* canopy_core_names(const char* gameFolder, const char* kind, const char* language) {
    try {
        return Hand(game_assets::NamesJson(PathArg(gameFolder), kind ? kind : "", language ? language : ""));
    } catch (...) {
        return Failure("The names could not be read.");
    }
}

char* canopy_core_sound(const char* gameFolder, const char* gamePath, unsigned char** bytes) {
    if (bytes) {
        *bytes = nullptr;
    }
    try {
        if (!bytes) {
            return Hand(game_browse::FailureJson("No place for the bytes was given."));
        }
        std::vector<uint8_t> ogg;
        std::string error;
        if (!game_assets::Sound(PathArg(gameFolder), gamePath ? gamePath : "", ogg, error)) {
            return Hand(game_browse::FailureJson(error));
        }
        unsigned char* copy = HandBytes(ogg);
        if (!copy) {
            return Hand(game_browse::FailureJson("Out of memory for the sound."));
        }
        *bytes = copy;
        return Hand(
            "{\"abi\":" + std::to_string(mod_rules::kJsonAbi) + ",\"ok\":true,\"error\":null,\"size\":" +
            std::to_string(ogg.size()) + "}"
        );
    } catch (...) {
        if (*bytes) {
            std::free(*bytes);
            *bytes = nullptr;
        }
        return Failure("The sound could not be read.");
    }
}

char* canopy_core_ini_parse(const char* text) {
    try {
        return Hand(ini_document::ParseJson(text ? text : ""));
    } catch (...) {
        return Failure("The text could not be read.");
    }
}

char* canopy_core_ini_edit(const char* text, const char* op, const char* section, const char* key, const char* value) {
    try {
        return Hand(ini_document::EditJson(
            text ? text : "", op ? op : "", section ? section : "", key ? key : "", value ? value : ""
        ));
    } catch (...) {
        return Failure("The edit could not be made.");
    }
}

void canopy_core_free(char* text) {
    std::free(text);
}

void canopy_core_free_pixels(unsigned char* pixels) {
    std::free(pixels);
}

void canopy_core_free_bytes(unsigned char* bytes) {
    std::free(bytes);
}

}  // extern "C"
