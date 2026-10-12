#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace chromalis {
class Log final {
public:
    explicit Log(const std::filesystem::path& path);
    void Write(std::string_view text) noexcept;
    const std::filesystem::path& Path() const { return path_; }
private:
    std::filesystem::path path_;
    std::ofstream stream_;
};
std::string Utf8(std::wstring_view value);
std::string HresultText(HRESULT result);
void Check(HRESULT result, std::string_view operation);
void CheckWin32(BOOL result, std::string_view operation);
}
