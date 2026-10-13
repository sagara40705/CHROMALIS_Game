#include "cooker/CookStatic.h"
#include <fstream>
#include <iostream>
#include <windows.h>

namespace {
bool SameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
    // Windows path spelling/case and existing hard links must both be checked.
    const auto x = std::filesystem::weakly_canonical(a).wstring();
    const auto y = std::filesystem::weakly_canonical(b).wstring();
    return CompareStringOrdinal(x.c_str(), -1, y.c_str(), -1, TRUE) == CSTR_EQUAL ||
        (std::filesystem::exists(a) && std::filesystem::exists(b) && std::filesystem::equivalent(a,b));
}
}

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path report;
    const auto print = [&](const std::string& message) {
        std::cout << message << '\n';
    };
    try {
        if ((argc != 5 && argc != 7) || std::wstring_view(argv[3]) != L"--id" ||
            (argc == 7 && (std::wstring_view(argv[5]) != L"--report" || !*argv[6])))
            throw std::runtime_error("Usage: ChromalisAssetCooker source.gltf|glb output.cmesh|cskin --id stable/id [--report report.txt]");
        if (argc == 7) report = argv[6];
        const auto id8 = std::filesystem::path(argv[4]).u8string();
        // Until import succeeds the dependency set is unknown. Errors go only to stdout.
        const std::string id(reinterpret_cast<const char*>(id8.data()), id8.size());
        const bool skinned=std::filesystem::path(argv[2]).extension()==L".cskin";
        chromalis::SkeletalAsset skin;
        if(skinned)skin=chromalis::CookSkeletal(argv[1],id);
        auto a = skinned?skin.mesh:chromalis::CookStatic(argv[1],id);
        const auto output = std::filesystem::weakly_canonical(argv[2]);
        const auto sourceDirectory = std::filesystem::canonical(argv[1]).parent_path();
        if (!report.empty() && SameFile(output, report))
            throw std::runtime_error("Report must not overlap cooked output");
        for (const auto& dependency : a.dependencies) {
            if (dependency.relativePath == "@recipe") continue;
            const auto sourcePath = sourceDirectory / std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(dependency.relativePath.data()), dependency.relativePath.size()));
            if (SameFile(output,sourcePath) || (!report.empty() && SameFile(report,sourcePath)))
                throw std::runtime_error("Output/report must not overwrite an input or source dependency");
        }
        // All destination collisions are checked before either file is modified.
        // Reserve the report without truncating it. Deny concurrent writes/deletion while publishing.
        struct ReportHandle { HANDLE value = INVALID_HANDLE_VALUE; ~ReportHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); } } handle;
        if (!report.empty()) {
            handle.value = CreateFileW(report.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open validated report path");
        }
        if(skinned)chromalis::SaveSkeletalAsset(skin,argv[2]);
        else chromalis::SaveStaticAsset(a, argv[2]);
        const auto message = "Cook OK: " + a.id + " | vertices=" + std::to_string(a.vertices.size()) + " indices=" + std::to_string(a.indices.size()) + " parts=" + std::to_string(a.parts.size()) + " textures=" + std::to_string(a.textures.size()) + " | base-color preview; PBR lighting/maps, mipmaps and vertex colors are not evaluated in 0B";
        if (handle.value != INVALID_HANDLE_VALUE) {
            const auto line = message + '\n'; DWORD written = 0;
            if (!WriteFile(handle.value,line.data(),static_cast<DWORD>(line.size()),&written,nullptr) || written != line.size() || !SetEndOfFile(handle.value))
                throw std::runtime_error("Cooked output published, but report write failed");
        }
        print(message);
        return 0;
    } catch (const std::exception& e) { print(std::string("Cook FAILED: ") + e.what()); return 1; }
}
