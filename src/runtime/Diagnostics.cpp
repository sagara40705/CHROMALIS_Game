#include "runtime/Diagnostics.h"
#include <format>
#include <stdexcept>

namespace chromalis {
Log::Log(const std::filesystem::path& path) : path_(path) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    stream_.open(path, std::ios::out | std::ios::trunc);
    if (!stream_) throw std::runtime_error("Cannot open log: " + path.string());
}
void Log::Write(std::string_view text) noexcept {
    try {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        const auto line = std::format("[{:02}:{:02}:{:02}.{:03}] {}\n", now.wHour, now.wMinute,
                                      now.wSecond, now.wMilliseconds, text);
        stream_ << line;
        stream_.flush();
        OutputDebugStringA(line.c_str());
    } catch (...) { OutputDebugStringA("CHROMALIS: logging failed\n"); }
}
std::string Utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Invalid UTF-16 text");
    std::string output(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        output.data(), count, nullptr, nullptr);
    return output;
}
std::string HresultText(HRESULT result) {
    wchar_t message[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
        static_cast<DWORD>(result), 0, message, 512, nullptr);
    return std::format("0x{:08X} {}", static_cast<unsigned int>(result), Utf8(message));
}
void Check(HRESULT result, std::string_view operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + ": " + HresultText(result));
}
void CheckWin32(BOOL result, std::string_view operation) {
    if (!result) {
        const auto error = GetLastError();
        Check(HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE), operation);
    }
}
}
