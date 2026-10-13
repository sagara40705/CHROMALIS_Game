#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <stdexcept>

namespace chromalis {
// GPUの完了を保証できない失敗は、アセット更新失敗として握りつぶさない。
class GpuFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
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
