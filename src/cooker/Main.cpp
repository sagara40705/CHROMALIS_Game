#include "cooker/CookStatic.h"
#include <fstream>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    std::filesystem::path report;
    if (argc == 7 && std::wstring_view(argv[5]) == L"--report") report = argv[6];
    const auto print = [&](const std::string& message) {
        std::cout << message << '\n';
        if (!report.empty()) { std::ofstream out(report, std::ios::trunc); out << message << '\n'; }
    };
    try {
        if ((argc != 5 && argc != 7) || std::wstring_view(argv[3]) != L"--id" || (argc == 7 && report.empty()))
            throw std::runtime_error("Usage: ChromalisAssetCooker source.gltf|glb output.cmesh --id stable/id [--report report.txt]");
        const auto id8 = std::filesystem::path(argv[4]).u8string();
        auto a = chromalis::CookStatic(argv[1], std::string(reinterpret_cast<const char*>(id8.data()), id8.size()));
        const auto output = std::filesystem::weakly_canonical(argv[2]);
        const auto sourceDirectory = std::filesystem::canonical(argv[1]).parent_path();
        for (const auto& dependency : a.dependencies) {
            if (dependency.relativePath == "@recipe") continue;
            const auto sourcePath = sourceDirectory / std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(dependency.relativePath.data()), dependency.relativePath.size()));
            if (output == std::filesystem::weakly_canonical(sourcePath) ||
                (std::filesystem::exists(output) && std::filesystem::equivalent(output,sourcePath)))
                throw std::runtime_error("Cooked output must not overwrite a source dependency");
        }
        chromalis::SaveStaticAsset(a, argv[2]);
        print("Cook OK: " + a.id + " | vertices=" + std::to_string(a.vertices.size()) + " indices=" + std::to_string(a.indices.size()) + " parts=" + std::to_string(a.parts.size()) + " textures=" + std::to_string(a.textures.size()) + " | base-color preview; PBR lighting/maps, mipmaps and vertex colors are not evaluated in 0B");
        return 0;
    } catch (const std::exception& e) { print(std::string("Cook FAILED: ") + e.what()); return 1; }
}
