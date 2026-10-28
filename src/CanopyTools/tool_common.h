#pragma once
#include <filesystem>
#include <string>
#include <vector>

// What the canopy commands share.
namespace canopy_tool {

// canopy dump list|extract ...: the archive lister, args after "dump".
int RunDump(const std::vector<std::wstring>& args);
// canopy pack <mod folder> <out.zip>.
int RunPack(const std::vector<std::wstring>& args);

std::string Utf8(const std::wstring& text);
// True when path is folder or inside it.
bool InsideFolder(const std::filesystem::path& path, const std::filesystem::path& folder);

}  // namespace canopy_tool
