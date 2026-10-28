#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>

#include "platform_files.h"
#include "tool_common.h"

// Packs a mod folder into a zip a player unzips into the game's mods
// folder: canopy pack <mod folder> <out.zip>. Every file is stored, not
// compressed, under <folder name>/, with UTF-8 names. No zip64, so a mod
// stays under 4 GB and 65535 files.
namespace canopy_tool {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kMaxZipBytes = 0xFFFFFFFFull;
constexpr size_t kMaxEntries = 0xFFFF;
// The zip version that can store files: 2.0.
constexpr uint16_t kZipVersion = 20;
// General purpose flag bit 11: names are UTF-8.
constexpr uint16_t kUtf8Names = 0x0800;

uint32_t Crc32(const std::vector<uint8_t>& bytes) {
    static uint32_t table[256] = {};
    static bool built = false;
    if (!built) {
        for (uint32_t index = 0; index < 256; ++index) {
            uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1) ? 0xEDB88320u ^ (value >> 1) : value >> 1;
            }
            table[index] = value;
        }
        built = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (const uint8_t byte : bytes) {
        crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void Put16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void Put32(std::vector<uint8_t>& out, uint32_t value) {
    Put16(out, static_cast<uint16_t>(value));
    Put16(out, static_cast<uint16_t>(value >> 16));
}

// The file's last write time as MS-DOS time and date, local time.
void DosTime(const fs::path& path, uint16_t& time, uint16_t& date) {
    time = 0;
    date = (1 << 5) | 1;  // 1980-01-01
    platform::LocalTime parts;
    if (!platform::LocalWriteTime(path.wstring(), parts) || parts.year < 1980) {
        return;
    }
    time = static_cast<uint16_t>((parts.hour << 11) | (parts.minute << 5) | (parts.second / 2));
    date = static_cast<uint16_t>(((parts.year - 1980) << 9) | (parts.month << 5) | parts.day);
}

struct Entry {
    fs::path path;
    std::string name;
};

struct Written {
    std::string name;
    uint32_t crc = 0;
    uint32_t size = 0;
    uint32_t offset = 0;
    uint16_t time = 0;
    uint16_t date = 0;
};

int Fail(const std::string& message) {
    std::fprintf(stderr, "error: %s\n", message.c_str());
    return 1;
}

}  // namespace

int RunPack(const std::vector<std::wstring>& args) {
    if (args.size() != 2) {
        std::fprintf(stderr, "usage:\n  canopy pack <mod folder> <out.zip>\n");
        return 2;
    }
    std::error_code error;
    const fs::path folder = fs::weakly_canonical(fs::absolute(args[0], error), error);
    const fs::path out = fs::absolute(args[1], error);
    if (!fs::is_directory(folder, error)) {
        return Fail(Utf8(args[0]) + " is not a folder.");
    }
    if (!platform::IsFile(platform::Join(folder.wstring(), L"canopy.ini"))) {
        return Fail(Utf8(folder.filename().wstring()) + " has no canopy.ini, so it is not a Canopy mod folder.");
    }
    if (InsideFolder(out, folder)) {
        return Fail("the zip cannot go inside the folder it packs.");
    }
    const std::string root = Utf8(folder.filename().wstring());

    std::vector<Entry> entries;
    for (fs::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) {
            continue;
        }
        std::string name = Utf8(fs::relative(it->path(), folder, error).wstring());
        std::replace(name.begin(), name.end(), '\\', '/');
        entries.push_back({it->path(), root + "/" + name});
    }
    if (error) {
        return Fail("the folder could not be listed: " + error.message());
    }
    if (entries.size() > kMaxEntries) {
        return Fail("a zip without zip64 holds at most 65535 files.");
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        return left.name < right.name;
    });

    fs::path part = out;
    part += L".part";
    std::ofstream file(part, std::ios::binary | std::ios::trunc);
    if (!file) {
        return Fail("cannot write " + Utf8(part.wstring()) + ".");
    }
    std::vector<Written> written;
    uint64_t offset = 0;
    uint64_t total = 0;
    for (const Entry& entry : entries) {
        std::ifstream input(entry.path, std::ios::binary | std::ios::ate);
        const std::streamoff size = input ? static_cast<std::streamoff>(input.tellg()) : -1;
        if (size < 0) {
            file.close();
            fs::remove(part, error);
            return Fail("cannot read " + entry.name + ".");
        }
        if (offset + 30 + entry.name.size() + static_cast<uint64_t>(size) > kMaxZipBytes) {
            file.close();
            fs::remove(part, error);
            return Fail("a zip without zip64 holds at most 4 GB.");
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        input.seekg(0);
        if (size > 0 && !input.read(reinterpret_cast<char*>(bytes.data()), size)) {
            file.close();
            fs::remove(part, error);
            return Fail("cannot read " + entry.name + ".");
        }
        Written record;
        record.name = entry.name;
        record.crc = Crc32(bytes);
        record.size = static_cast<uint32_t>(bytes.size());
        record.offset = static_cast<uint32_t>(offset);
        DosTime(entry.path, record.time, record.date);

        std::vector<uint8_t> header;
        Put32(header, 0x04034B50);
        Put16(header, kZipVersion);
        Put16(header, kUtf8Names);
        Put16(header, 0);  // stored
        Put16(header, record.time);
        Put16(header, record.date);
        Put32(header, record.crc);
        Put32(header, record.size);
        Put32(header, record.size);
        Put16(header, static_cast<uint16_t>(record.name.size()));
        Put16(header, 0);
        header.insert(header.end(), record.name.begin(), record.name.end());
        file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        offset += header.size() + bytes.size();
        total += bytes.size();
        written.push_back(std::move(record));
    }

    std::vector<uint8_t> directory;
    for (const Written& record : written) {
        Put32(directory, 0x02014B50);
        Put16(directory, kZipVersion);
        Put16(directory, kZipVersion);
        Put16(directory, kUtf8Names);
        Put16(directory, 0);
        Put16(directory, record.time);
        Put16(directory, record.date);
        Put32(directory, record.crc);
        Put32(directory, record.size);
        Put32(directory, record.size);
        Put16(directory, static_cast<uint16_t>(record.name.size()));
        Put16(directory, 0);  // extra
        Put16(directory, 0);  // comment
        Put16(directory, 0);  // disk
        Put16(directory, 0);  // internal attributes
        Put32(directory, 0);  // external attributes
        Put32(directory, record.offset);
        directory.insert(directory.end(), record.name.begin(), record.name.end());
    }
    if (offset + directory.size() + 22 > kMaxZipBytes) {
        file.close();
        fs::remove(part, error);
        return Fail("a zip without zip64 holds at most 4 GB.");
    }
    const uint32_t directorySize = static_cast<uint32_t>(directory.size());
    Put32(directory, 0x06054B50);
    Put16(directory, 0);  // this disk
    Put16(directory, 0);  // the directory's disk
    Put16(directory, static_cast<uint16_t>(written.size()));
    Put16(directory, static_cast<uint16_t>(written.size()));
    Put32(directory, directorySize);
    Put32(directory, static_cast<uint32_t>(offset));
    Put16(directory, 0);  // comment
    file.write(reinterpret_cast<const char*>(directory.data()), static_cast<std::streamsize>(directory.size()));
    file.close();
    if (!file) {
        fs::remove(part, error);
        return Fail("cannot write " + Utf8(part.wstring()) + ".");
    }
    if (!platform::RenameOver(part.wstring(), out.wstring())) {
        fs::remove(part, error);
        return Fail("cannot write " + Utf8(out.wstring()) + ".");
    }
    std::printf(
        "Packed %zu files, %llu bytes, from %s into %s. Unzip it into the game's mods folder.\n", written.size(),
        static_cast<unsigned long long>(total), root.c_str(), Utf8(out.wstring()).c_str()
    );
    return 0;
}

}  // namespace canopy_tool
