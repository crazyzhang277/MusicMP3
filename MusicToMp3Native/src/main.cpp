#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define IDI_ICON1 1

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}

namespace fs = std::filesystem;

constexpr int IDC_FILES = 1001;
constexpr int IDC_ADD = 1002;
constexpr int IDC_FOLDER = 1003;
constexpr int IDC_CLEAR = 1004;
constexpr int IDC_OUTPUT = 1005;
constexpr int IDC_BROWSE = 1006;
constexpr int IDC_BITRATE = 1007;
constexpr int IDC_CONVERT = 1008;
constexpr int IDC_PROGRESS = 1009;
constexpr int IDC_STATUS = 1010;
constexpr int IDC_TITLE = 1011;
constexpr int IDC_SUBTITLE = 1012;
constexpr int IDC_OUTPUT_LABEL = 1013;
constexpr int IDC_BITRATE_LABEL = 1014;
constexpr int IDC_INPUT_LABEL = 1015;
constexpr int IDC_OUTPUT_GROUP_LABEL = 1016;
constexpr int IDC_KBPS_LABEL = 1017;
constexpr UINT WM_CONVERT_STATUS = WM_APP + 1;
constexpr UINT WM_CONVERT_DONE = WM_APP + 2;
constexpr UINT WM_FILES_READY = WM_APP + 3;

struct StatusMessage { std::wstring text; int progress = -1; };

HWND g_main = nullptr;
HWND g_files = nullptr;
HWND g_output = nullptr;
HWND g_bitrate = nullptr;
HWND g_progress = nullptr;
HWND g_status = nullptr;
HFONT g_font = nullptr;
HFONT g_titleFont = nullptr;
HFONT g_sectionFont = nullptr;
HBRUSH g_backgroundBrush = nullptr;
HBRUSH g_controlBrush = nullptr;
HBRUSH g_panelBrush = nullptr;

struct LayoutMetrics {
    int width = 861;
    int height = 672;
    int outputTop = 414;
    int outputBottom = 556;
    int fileBottom = 398;
    int listBottom = 334;
    int buttonY = 356;
    int progressY = 574;
    int statusY = 596;
};

LayoutMetrics g_layout;

LayoutMetrics make_layout(int width, int height) {
    LayoutMetrics layout;
    layout.width = std::max(width, 798);
    layout.height = std::max(height, 609);
    layout.outputBottom = layout.height - 104;
    layout.outputTop = layout.outputBottom - 142;
    layout.fileBottom = layout.outputTop - 16;
    layout.listBottom = layout.fileBottom - 64;
    layout.buttonY = layout.fileBottom - 54;
    layout.progressY = layout.outputBottom + 18;
    layout.statusY = layout.progressY + 22;
    return layout;
}

void move_control(HWND control, int x, int y, int width, int height) {
    if (control) SetWindowPos(control, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void layout_controls(HWND window) {
    RECT client{};
    GetClientRect(window, &client);
    g_layout = make_layout(client.right, client.bottom);
    const int right = g_layout.width - 24;
    const int contentWidth = right - 44;
    move_control(GetDlgItem(window, IDC_TITLE), 44, 24, 420, 42);
    move_control(GetDlgItem(window, IDC_SUBTITLE), 46, 68, 600, 24);
    move_control(GetDlgItem(window, IDC_INPUT_LABEL), 44, 112, 160, 24);
    move_control(g_files, 44, 144, contentWidth, std::max(90, g_layout.listBottom - 144));
    move_control(GetDlgItem(window, IDC_ADD), 44, g_layout.buttonY, 122, 38);
    move_control(GetDlgItem(window, IDC_FOLDER), 178, g_layout.buttonY, 132, 38);
    move_control(GetDlgItem(window, IDC_CLEAR), 322, g_layout.buttonY, 110, 38);
    move_control(GetDlgItem(window, IDC_OUTPUT_GROUP_LABEL), 44, g_layout.outputTop + 16, 160, 24);
    move_control(GetDlgItem(window, IDC_OUTPUT_LABEL), 44, g_layout.outputTop + 54, 82, 24);
    const int browseX = right - 148;
    move_control(g_output, 142, g_layout.outputTop + 48, browseX - 156, 32);
    move_control(GetDlgItem(window, IDC_BROWSE), browseX, g_layout.outputTop + 46, 148, 36);
    move_control(GetDlgItem(window, IDC_BITRATE_LABEL), 44, g_layout.outputTop + 98, 82, 24);
    move_control(g_bitrate, 142, g_layout.outputTop + 92, 170, 160);
    move_control(GetDlgItem(window, IDC_KBPS_LABEL), 326, g_layout.outputTop + 98, 60, 24);
    move_control(GetDlgItem(window, IDC_CONVERT), right - 244, g_layout.outputTop + 90, 244, 40);
    move_control(g_progress, 44, g_layout.progressY, contentWidth, 10);
    move_control(g_status, 44, g_layout.statusY, contentWidth, 28);
}
std::vector<fs::path> g_paths;
std::mutex g_pathsMutex;
std::atomic_bool g_converting = false;

void apply_font(HWND control, HFONT font = g_font) {
    if (control && font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void draw_panel(HDC dc, const RECT& rect) {
    const auto fill = CreateSolidBrush(RGB(255, 255, 255));
    const auto border = CreatePen(PS_SOLID, 1, RGB(226, 232, 240));
    const auto oldBrush = SelectObject(dc, fill);
    const auto oldPen = SelectObject(dc, border);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 10, 10);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(border);
    DeleteObject(fill);
}

void draw_button(const DRAWITEMSTRUCT& item) {
    const bool primary = item.CtlID == IDC_CONVERT;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    RECT rect = item.rcItem;
    InflateRect(&rect, -1, -1);
    const COLORREF fillColor = disabled ? RGB(226, 232, 240)
        : primary ? (pressed ? RGB(15, 86, 150) : RGB(24, 110, 190))
        : (pressed ? RGB(219, 234, 254) : RGB(255, 255, 255));
    const COLORREF borderColor = disabled ? RGB(203, 213, 225)
        : primary ? RGB(24, 110, 190) : RGB(203, 213, 225);
    const COLORREF textColor = disabled ? RGB(148, 163, 184)
        : primary ? RGB(255, 255, 255) : RGB(30, 41, 59);
    const auto fill = CreateSolidBrush(fillColor);
    const auto border = CreatePen(PS_SOLID, 1, borderColor);
    const auto oldBrush = SelectObject(item.hDC, fill);
    const auto oldPen = SelectObject(item.hDC, border);
    RoundRect(item.hDC, rect.left, rect.top, rect.right, rect.bottom, 7, 7);
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(border);
    DeleteObject(fill);

    wchar_t text[128]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, textColor);
    DrawTextW(item.hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (item.itemState & ODS_FOCUS) {
        RECT focus = rect;
        InflateRect(&focus, -4, -4);
        DrawFocusRect(item.hDC, &focus);
    }
}

int selected_bitrate() {
    const auto index = static_cast<int>(SendMessageW(g_bitrate, CB_GETCURSEL, 0, 0));
    if (index < 0) return 320;
    wchar_t value[32]{};
    SendMessageW(g_bitrate, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(value));
    const int bitrate = _wtoi(value);
    return bitrate > 0 ? bitrate : 320;
}

std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

void set_status(const std::wstring& text, int progress = -1) {
    PostMessageW(g_main, WM_CONVERT_STATUS, 0, reinterpret_cast<LPARAM>(new StatusMessage{text, progress}));
}

void handle_output_directory_drop(HWND edit, HDROP drop) {
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    if (count != 1) {
        SetWindowTextW(g_status, L"请一次拖入一个文件夹作为输出目录");
        DragFinish(drop);
        return;
    }
    const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
    std::wstring path(length + 1, L'\0');
    DragQueryFileW(drop, 0, path.data(), static_cast<UINT>(path.size()));
    path.resize(length);
    std::error_code error;
    if (!fs::is_directory(fs::path(path), error) || error) {
        SetWindowTextW(g_status, L"只能将文件夹拖到输出目录");
    } else {
        SetWindowTextW(edit, path.c_str());
        SetWindowTextW(g_status, L"输出目录已设置");
    }
    DragFinish(drop);
}

LRESULT CALLBACK output_edit_subclass(HWND edit, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_DROPFILES) {
        handle_output_directory_drop(edit, reinterpret_cast<HDROP>(wParam));
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(edit, output_edit_subclass, 1);
    return DefSubclassProc(edit, message, wParam, lParam);
}

std::wstring extension_lower(const fs::path& path) {
    auto ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), towlower);
    return ext;
}

bool supported(const fs::path& path) {
    static const std::vector<std::wstring> extensions = {
        L".aac", L".ape", L".aiff", L".flac", L".m4a", L".mp4", L".ncm",
        L".ogg", L".opus", L".wav", L".wma", L".mp3"
    };
    const auto ext = extension_lower(path);
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

void refresh_file_list() {
    SendMessageW(g_files, LB_RESETCONTENT, 0, 0);
    std::lock_guard lock(g_pathsMutex);
    for (const auto& path : g_paths)
        SendMessageW(g_files, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(path.filename().wstring().c_str()));
}

std::vector<fs::path> expand_paths(const std::vector<fs::path>& paths) {
    std::vector<fs::path> expanded;
    for (const auto& input : paths) {
        std::error_code error;
        if (fs::is_directory(input, error)) {
            for (const auto& entry : fs::directory_iterator(input, fs::directory_options::skip_permission_denied, error)) {
                if (!error && entry.is_regular_file(error) && supported(entry.path()))
                    expanded.push_back(entry.path());
            }
        } else if (!error && fs::is_regular_file(input, error) && supported(input)) {
            expanded.push_back(input);
        }
    }
    return expanded;
}

void add_paths(const std::vector<fs::path>& paths) {
    const auto expanded = expand_paths(paths);
    bool added = false;
    {
        std::lock_guard lock(g_pathsMutex);
        for (const auto& path : expanded) {
            if (std::find(g_paths.begin(), g_paths.end(), path) == g_paths.end()) {
                g_paths.push_back(path);
                added = true;
            }
        }
    }
    refresh_file_list();
    if (added) SendMessageW(g_progress, PBM_SETPOS, 0, 0);
}

void merge_paths(const std::vector<fs::path>& paths) {
    bool added = false;
    {
        std::lock_guard lock(g_pathsMutex);
        for (const auto& path : paths) {
            if (std::find(g_paths.begin(), g_paths.end(), path) == g_paths.end()) {
                g_paths.push_back(path);
                added = true;
            }
        }
    }
    refresh_file_list();
    if (added) SendMessageW(g_progress, PBM_SETPOS, 0, 0);
}

void add_folder_async(const fs::path& folder) {
    SetWindowTextW(g_status, L"正在读取文件夹...");
    EnableWindow(GetDlgItem(g_main, IDC_FOLDER), FALSE);
    std::thread([folder] {
        auto expanded = expand_paths({folder});
        PostMessageW(g_main, WM_FILES_READY, 0, reinterpret_cast<LPARAM>(new std::vector<fs::path>(std::move(expanded))));
    }).detach();
}

std::vector<fs::path> choose_files(HWND owner) {
    std::vector<fs::path> result;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return result;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    COMDLG_FILTERSPEC filters[] = {{L"音频文件", L"*.aac;*.ape;*.aiff;*.flac;*.m4a;*.mp4;*.mp3;*.ncm;*.ogg;*.opus;*.wav;*.wma"}, {L"所有文件", L"*.*"}};
    dialog->SetFileTypes(2, filters);
    if (FAILED(dialog->Show(owner))) { dialog->Release(); return result; }
    IShellItemArray* items = nullptr;
    if (SUCCEEDED(dialog->GetResults(&items))) {
        DWORD count = 0;
        items->GetCount(&count);
        for (DWORD i = 0; i < count; ++i) {
            IShellItem* item = nullptr;
            PWSTR name = nullptr;
            if (SUCCEEDED(items->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
                result.emplace_back(name);
                CoTaskMemFree(name);
            }
            if (item) item->Release();
        }
        items->Release();
    }
    dialog->Release();
    return result;
}

std::vector<fs::path> choose_folder(HWND owner) {
    std::vector<fs::path> result;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return result;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        PWSTR name = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
            result.emplace_back(name);
            CoTaskMemFree(name);
        }
        if (item) item->Release();
    }
    dialog->Release();
    return result;
}

std::wstring choose_output(HWND owner, const std::wstring& current) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return current;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
    if (FAILED(dialog->Show(owner))) { dialog->Release(); return current; }
    IShellItem* item = nullptr;
    PWSTR name = nullptr;
    std::wstring result = current;
    if (SUCCEEDED(dialog->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
        result = name;
        CoTaskMemFree(name);
    }
    if (item) item->Release();
    dialog->Release();
    return result;
}

std::wstring expand_environment(const std::wstring& value) {
    wchar_t buffer[32768]{};
    const DWORD length = ExpandEnvironmentStringsW(value.c_str(), buffer, static_cast<DWORD>(std::size(buffer)));
    return length > 0 && length < std::size(buffer) ? std::wstring(buffer, length - 1) : value;
}

std::vector<uint8_t> read_bytes(std::ifstream& file, size_t count) {
    std::vector<uint8_t> data(count);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(count));
    data.resize(static_cast<size_t>(file.gcount()));
    return data;
}

bool starts_with(const std::vector<uint8_t>& data, const char* text) {
    const size_t length = strlen(text);
    return data.size() >= length && std::equal(data.begin(), data.begin() + length, text);
}

std::vector<uint8_t> aes_ecb_decrypt(const std::vector<uint8_t>& encrypted, const std::vector<uint8_t>& key) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE keyHandle = nullptr;
    DWORD objectSize = 0, resultSize = 0;
    std::vector<uint8_t> output(encrypted.size());
    BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0);
    BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_ECB)), sizeof(BCRYPT_CHAIN_MODE_ECB), 0);
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &resultSize, 0);
    std::vector<uint8_t> object(objectSize);
    BCryptGenerateSymmetricKey(algorithm, &keyHandle, object.data(), objectSize, const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0);
    ULONG written = 0;
    BCryptDecrypt(keyHandle, const_cast<PUCHAR>(encrypted.data()), static_cast<ULONG>(encrypted.size()), nullptr, nullptr, 0, output.data(), static_cast<ULONG>(output.size()), &written, 0);
    output.resize(written);
    if (keyHandle) BCryptDestroyKey(keyHandle);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return output;
}

std::vector<uint8_t> rc4_decrypt(const std::vector<uint8_t>& key, const std::vector<uint8_t>& encrypted) {
    std::vector<uint8_t> box(256), keyBox(256), output(encrypted.size());
    for (int i = 0; i < 256; ++i) box[i] = static_cast<uint8_t>(i);
    int j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j + box[i] + key[i % key.size()]) & 255;
        std::swap(box[i], box[j]);
    }
    for (int i = 0; i < 256; ++i) {
        j = (i + 1) & 255;
        const auto sj = box[j];
        const auto sjj = box[(sj + j) & 255];
        keyBox[i] = box[(sjj + sj) & 255];
    }
    for (size_t i = 0; i < encrypted.size(); ++i) output[i] = encrypted[i] ^ keyBox[i & 255];
    return output;
}

std::vector<uint8_t> remove_padding(std::vector<uint8_t> data) {
    if (data.empty()) return {};
    const uint8_t count = data.back();
    if (count == 0 || count > 16 || count > data.size()) return {};
    if (!std::all_of(data.end() - count, data.end(), [count](uint8_t value) { return value == count; })) return {};
    data.resize(data.size() - count);
    return data;
}

std::string json_value(const std::string& json, const std::string& name, const std::string& fallback = {}) {
    const std::regex pattern("\\\"" + name + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch match;
    return std::regex_search(json, match, pattern) ? match[1].str() : fallback;
}

std::string sniff_audio_format(const std::vector<uint8_t>& audio) {
    if (audio.size() >= 3 && audio[0] == 'I' && audio[1] == 'D' && audio[2] == '3') return "mp3";
    if (audio.size() >= 2 && audio[0] == 0xff && (audio[1] & 0xe0) == 0xe0) return "mp3";
    if (audio.size() >= 4 && audio[0] == 'f' && audio[1] == 'L' && audio[2] == 'a' && audio[3] == 'C') return "flac";
    if (audio.size() >= 4 && audio[0] == 'O' && audio[1] == 'g' && audio[2] == 'g' && audio[3] == 'S') return "ogg";
    if (audio.size() >= 4 && audio[0] == 'R' && audio[1] == 'I' && audio[2] == 'F' && audio[3] == 'F') return "wav";
    if (audio.size() >= 8 && audio[4] == 'f' && audio[5] == 't' && audio[6] == 'y' && audio[7] == 'p') return "m4a";
    if (audio.size() >= 2 && audio[0] == 0xff && (audio[1] & 0xf6) == 0xf0) return "aac";
    return {};
}

struct NcmTrack { fs::path audioPath; std::string format; std::string title; std::string artist; std::string album; };

bool decode_ncm(const fs::path& source, NcmTrack& track, std::wstring& error) {
    std::ifstream file(source, std::ios::binary);
    if (!file) { error = L"无法打开 NCM 文件"; return false; }
    const auto magic = read_bytes(file, 8);
    if (!starts_with(magic, "CTENFDAM")) { error = L"不是有效的 NCM 文件"; return false; }
    file.seekg(2, std::ios::cur);
    auto block = [&](const char* name) {
        uint32_t size = 0;
        file.read(reinterpret_cast<char*>(&size), sizeof(size));
        if (!file || size > 64 * 1024 * 1024) throw std::runtime_error(name);
        return read_bytes(file, size);
    };
    try {
        auto encryptedKey = block("NCM 密钥");
        auto encryptedMetadata = block("NCM 元数据");
        file.seekg(9, std::ios::cur);
        auto cover = block("NCM 封面");
        (void)cover;
        const auto position = file.tellg();
        file.seekg(0, std::ios::end);
        const auto audioSize = static_cast<size_t>(file.tellg() - position);
        file.seekg(position);
        auto encryptedAudio = read_bytes(file, audioSize);
        for (auto& byte : encryptedKey) byte ^= 0x64;
        const auto keyData = remove_padding(aes_ecb_decrypt(encryptedKey, {0x68,0x7a,0x48,0x52,0x41,0x6d,0x73,0x6f,0x35,0x6b,0x49,0x6e,0x62,0x61,0x78,0x57}));
        const std::vector<uint8_t> keyPrefix{'n','e','t','e','a','s','e','c','l','o','u','d','m','u','s','i','c'};
        if (keyData.size() <= keyPrefix.size() || !std::equal(keyPrefix.begin(), keyPrefix.end(), keyData.begin())) throw std::runtime_error("密钥校验失败");
        for (auto& byte : encryptedMetadata) byte ^= 0x63;
        std::string metadata;
        if (!encryptedMetadata.empty()) {
            const std::string prefix = "163 key(Don't modify):";
            if (!starts_with(encryptedMetadata, prefix.c_str())) throw std::runtime_error("元数据校验失败");
            const auto encoded = std::string(encryptedMetadata.begin() + prefix.size(), encryptedMetadata.end());
            std::vector<uint8_t> decoded((encoded.size() / 4) * 3 + 3);
            DWORD decodedSize = static_cast<DWORD>(decoded.size());
            if (!CryptStringToBinaryA(encoded.c_str(), 0, CRYPT_STRING_BASE64, decoded.data(), &decodedSize, nullptr, nullptr)) throw std::runtime_error("元数据 Base64 无效");
            decoded.resize(decodedSize);
            auto plain = remove_padding(aes_ecb_decrypt(decoded, {0x23,0x31,0x34,0x6c,0x6a,0x6b,0x5f,0x21,0x5c,0x5d,0x26,0x30,0x55,0x3c,0x27,0x28}));
            metadata.assign(plain.begin(), plain.end());
        }
        auto audio = rc4_decrypt({keyData.begin() + keyPrefix.size(), keyData.end()}, encryptedAudio);
        if (audio.size() < 1024) throw std::runtime_error("NCM 解密后的音频数据为空或损坏");
        track.format = json_value(metadata, "format", "mp3");
        track.format.erase(std::remove_if(track.format.begin(), track.format.end(), [](unsigned char c) {
            return std::isspace(c) || c == '\"' || c == '\\';
        }), track.format.end());
        if (!track.format.empty() && track.format.front() == '.') track.format.erase(track.format.begin());
        track.title = json_value(metadata, "musicName", source.stem().string());
        track.album = json_value(metadata, "album");
        track.artist = json_value(metadata, "artist");
        std::replace(track.format.begin(), track.format.end(), '.', ' ');
        std::transform(track.format.begin(), track.format.end(), track.format.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto detectedFormat = sniff_audio_format(audio);
        if (!detectedFormat.empty()) track.format = detectedFormat;
        if (track.format.empty()) track.format = "mp3";
        const auto temp = fs::temp_directory_path() / ("musicmp3-native-" + std::to_string(GetCurrentProcessId()) + "." + track.format);
        std::ofstream output(temp, std::ios::binary);
        output.write(reinterpret_cast<const char*>(audio.data()), static_cast<std::streamsize>(audio.size()));
        track.audioPath = temp;
        return true;
    } catch (const std::exception& exception) {
        error = widen(exception.what());
        return false;
    }
}

std::string ffmpeg_error(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buffer, sizeof(buffer));
    return buffer;
}

bool convert_audio(const fs::path& source, const fs::path& target, int bitrate, std::wstring& error,
    const std::string& titleOverride = {}, const std::string& artistOverride = {}, const std::string& albumOverride = {}) {
    AVFormatContext* input = nullptr;
    AVCodecContext* decoder = nullptr;
    AVFormatContext* output = nullptr;
    AVCodecContext* encoder = nullptr;
    SwrContext* resampler = nullptr;
    AVAudioFifo* fifo = nullptr;
    AVDictionary* metadata = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* decoded = av_frame_alloc();
    AVFrame* converted = av_frame_alloc();
    bool success = false;
    const std::string inputName = utf8(source.wstring());
    const std::string outputName = utf8(target.wstring());
    int result = avformat_open_input(&input, inputName.c_str(), nullptr, nullptr);
    int streamIndex = -1;
    if (result < 0) { error = L"打开音频失败：" + widen(ffmpeg_error(result)); goto cleanup; }
    if ((result = avformat_find_stream_info(input, nullptr)) < 0) { error = L"读取音频信息失败：" + widen(ffmpeg_error(result)); goto cleanup; }
    streamIndex = av_find_best_stream(input, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0) { error = L"找不到音频流"; goto cleanup; }
    av_dict_copy(&metadata, input->metadata, 0);
    av_dict_copy(&metadata, input->streams[streamIndex]->metadata, AV_DICT_DONT_OVERWRITE);
    {
        const AVCodecParameters* parameters = input->streams[streamIndex]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
        if (!codec) { error = L"找不到输入格式解码器"; goto cleanup; }
        decoder = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(decoder, parameters);
        if ((result = avcodec_open2(decoder, codec, nullptr)) < 0) { error = L"打开解码器失败：" + widen(ffmpeg_error(result)); goto cleanup; }
    }
    if ((result = avformat_alloc_output_context2(&output, nullptr, "mp3", outputName.c_str())) < 0 || !output) { error = L"创建 MP3 输出失败"; goto cleanup; }
    {
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
        if (!codec) { error = L"当前 FFmpeg 没有 MP3 编码器"; goto cleanup; }
        encoder = avcodec_alloc_context3(codec);
        encoder->bit_rate = bitrate * 1000;
        encoder->sample_rate = decoder->sample_rate > 0 ? decoder->sample_rate : 44100;
        encoder->time_base = AVRational{1, encoder->sample_rate};
        encoder->sample_fmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        const int inputChannels = decoder->ch_layout.nb_channels > 0 ? decoder->ch_layout.nb_channels : 2;
        const int outputChannels = inputChannels == 1 ? 1 : 2;
        av_channel_layout_default(&encoder->ch_layout, outputChannels);
        AVStream* stream = avformat_new_stream(output, nullptr);
        if (!stream || (result = avcodec_open2(encoder, codec, nullptr)) < 0) { error = L"打开 MP3 编码器失败"; goto cleanup; }
        avcodec_parameters_from_context(stream->codecpar, encoder);
        stream->time_base = AVRational{1, encoder->sample_rate};
        if (!(output->oformat->flags & AVFMT_NOFILE) && (result = avio_open(&output->pb, outputName.c_str(), AVIO_FLAG_WRITE)) < 0) { error = L"无法创建输出文件"; goto cleanup; }
        const auto fallbackTitle = utf8(source.stem().wstring());
        if (!titleOverride.empty()) av_dict_set(&metadata, "title", titleOverride.c_str(), 0);
        else if (!av_dict_get(metadata, "title", nullptr, 0)) av_dict_set(&metadata, "title", fallbackTitle.c_str(), 0);
        if (!artistOverride.empty()) av_dict_set(&metadata, "artist", artistOverride.c_str(), 0);
        if (!albumOverride.empty()) av_dict_set(&metadata, "album", albumOverride.c_str(), 0);
        av_dict_copy(&output->metadata, metadata, 0);
        av_dict_free(&metadata);
        if ((result = avformat_write_header(output, nullptr)) < 0) { error = L"写入 MP3 文件头失败：" + widen(ffmpeg_error(result)); goto cleanup; }
        if ((result = swr_alloc_set_opts2(&resampler, &encoder->ch_layout, encoder->sample_fmt, encoder->sample_rate, &decoder->ch_layout, decoder->sample_fmt, decoder->sample_rate, 0, nullptr)) < 0 || (result = swr_init(resampler)) < 0) { error = L"初始化音频重采样失败"; goto cleanup; }
        fifo = av_audio_fifo_alloc(encoder->sample_fmt, encoder->ch_layout.nb_channels, 1);
        if (!fifo) { error = L"初始化音频缓冲失败"; goto cleanup; }
        int64_t pts = 0;
        auto encode_frame = [&](AVFrame* frame) {
            int r = avcodec_send_frame(encoder, frame);
            if (r < 0) return r;
            while ((r = avcodec_receive_packet(encoder, packet)) >= 0) {
                packet->stream_index = stream->index;
                av_packet_rescale_ts(packet, encoder->time_base, stream->time_base);
                r = av_interleaved_write_frame(output, packet);
                av_packet_unref(packet);
                if (r < 0) return r;
            }
            return r == AVERROR(EAGAIN) || r == AVERROR_EOF ? 0 : r;
        };
        auto drain_fifo = [&](bool flush) {
            while (true) {
                const int available = av_audio_fifo_size(fifo);
                if (available <= 0 || (!flush && available < encoder->frame_size)) break;
                const int samples = flush ? std::min(available, encoder->frame_size) : encoder->frame_size;
                AVFrame* frame = av_frame_alloc();
                if (!frame) return AVERROR(ENOMEM);
                frame->format = encoder->sample_fmt;
                frame->sample_rate = encoder->sample_rate;
                av_channel_layout_copy(&frame->ch_layout, &encoder->ch_layout);
                frame->nb_samples = samples;
                int r = av_frame_get_buffer(frame, 0);
                if (r >= 0 && av_audio_fifo_read(fifo, reinterpret_cast<void**>(frame->data), samples) != samples)
                    r = AVERROR(EIO);
                if (r >= 0) {
                    frame->pts = pts;
                    pts += samples;
                    r = encode_frame(frame);
                }
                av_frame_free(&frame);
                if (r < 0) return r;
            }
            return 0;
        };
        auto queue_frame = [&](AVFrame* frame) {
            if (av_audio_fifo_write(fifo, reinterpret_cast<void**>(frame->data), frame->nb_samples) < frame->nb_samples)
                return AVERROR(EIO);
            return drain_fifo(false);
        };
        int readResult = 0;
        result = 0;
        while ((readResult = av_read_frame(input, packet)) >= 0) {
            if (packet->stream_index == streamIndex) {
                const int sendResult = avcodec_send_packet(decoder, packet);
                if (sendResult < 0) { result = sendResult; av_packet_unref(packet); break; }
                int decodeResult = 0;
                while ((decodeResult = avcodec_receive_frame(decoder, decoded)) >= 0) {
                    const int samples = swr_get_out_samples(resampler, decoded->nb_samples);
                    av_frame_unref(converted);
                    converted->format = encoder->sample_fmt;
                    converted->sample_rate = encoder->sample_rate;
                    av_channel_layout_copy(&converted->ch_layout, &encoder->ch_layout);
                    converted->nb_samples = samples;
                    if ((result = av_frame_get_buffer(converted, 0)) < 0) break;
                    const int convertedSamples = swr_convert(resampler, converted->data, samples, const_cast<const uint8_t**>(decoded->extended_data), decoded->nb_samples);
                    if (convertedSamples < 0) { result = convertedSamples; break; }
                    converted->nb_samples = convertedSamples;
                    if ((result = queue_frame(converted)) < 0) break;
                }
                if (result >= 0 && decodeResult < 0 && decodeResult != AVERROR(EAGAIN) && decodeResult != AVERROR_EOF)
                    result = decodeResult;
            }
            av_packet_unref(packet);
            if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF) break;
        }
        if (readResult < 0 && readResult != AVERROR_EOF) result = readResult;
        if (result >= 0 || result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            result = 0;
            avcodec_send_packet(decoder, nullptr);
            int decodeResult = 0;
            while ((decodeResult = avcodec_receive_frame(decoder, decoded)) >= 0) {
                av_frame_unref(converted);
                converted->format = encoder->sample_fmt;
                converted->sample_rate = encoder->sample_rate;
                av_channel_layout_copy(&converted->ch_layout, &encoder->ch_layout);
                converted->nb_samples = swr_get_out_samples(resampler, decoded->nb_samples);
                if ((result = av_frame_get_buffer(converted, 0)) < 0) break;
                converted->nb_samples = swr_convert(resampler, converted->data, converted->nb_samples, const_cast<const uint8_t**>(decoded->extended_data), decoded->nb_samples);
                if (converted->nb_samples < 0) { result = converted->nb_samples; break; }
                if ((result = queue_frame(converted)) < 0) break;
            }
            if (result >= 0 && decodeResult < 0 && decodeResult != AVERROR(EAGAIN) && decodeResult != AVERROR_EOF)
                result = decodeResult;
            if (result >= 0 && (result = drain_fifo(true)) >= 0 && (result = encode_frame(nullptr)) >= 0 && (result = av_write_trailer(output)) >= 0)
                success = true;
        }
    }
cleanup:
    if (!success && error.empty() && result < 0) error = widen("音频转换失败：" + ffmpeg_error(result));
    if (!success && error.empty()) error = L"没有生成有效的音频帧";
    const bool removeIncompleteTarget = !success;
    if (resampler) swr_free(&resampler);
    if (fifo) av_audio_fifo_free(fifo);
    if (encoder) avcodec_free_context(&encoder);
    if (decoder) avcodec_free_context(&decoder);
    if (input) avformat_close_input(&input);
    if (output) {
        if (output->pb && !(output->oformat->flags & AVFMT_NOFILE)) avio_closep(&output->pb);
        avformat_free_context(output);
    }
    av_frame_free(&decoded);
    av_frame_free(&converted);
    av_packet_free(&packet);
    av_dict_free(&metadata);
    if (removeIncompleteTarget) {
        std::error_code removeError;
        fs::remove(target, removeError);
    }
    return success;
}

void convert_all(int bitrate) {
    std::vector<fs::path> paths;
    {
        std::lock_guard lock(g_pathsMutex);
        paths = g_paths;
    }
    wchar_t outputBuffer[MAX_PATH]{};
    GetWindowTextW(g_output, outputBuffer, MAX_PATH);
    const fs::path outputDirectory = outputBuffer[0]
        ? fs::path(expand_environment(outputBuffer))
        : fs::path(L".");
    fs::create_directories(outputDirectory);
    int failures = 0;
    std::wstring failureDetails;
    for (size_t i = 0; i < paths.size(); ++i) {
        set_status(L"正在转换 " + std::to_wstring(i + 1) + L"/" + std::to_wstring(paths.size()) + L"：" + paths[i].filename().wstring(), static_cast<int>(i * 100 / paths.size()));
        fs::path source = paths[i];
        NcmTrack track;
        std::wstring error;
        std::wstring outputStem = paths[i].stem().wstring();
        if (extension_lower(source) == L".ncm") {
            if (!decode_ncm(source, track, error)) {
                ++failures;
                const auto detail = L"失败：" + source.filename().wstring() + L" - " + error;
                failureDetails += detail + L"\n";
                set_status(detail);
                continue;
            }
            if (!track.title.empty()) outputStem = widen(track.title);
            source = track.audioPath;
        }
        fs::path target = outputDirectory / (outputStem + L".mp3");
        if (!convert_audio(source, target, bitrate, error, track.title, track.artist, track.album)) {
            ++failures;
            const auto detail = L"失败：" + paths[i].filename().wstring() + L" - " + error;
            failureDetails += detail + L"\n";
            set_status(detail);
        }
        if (!track.audioPath.empty()) fs::remove(track.audioPath);
        set_status(L"正在转换 " + std::to_wstring(i + 1) + L"/" + std::to_wstring(paths.size()), static_cast<int>((i + 1) * 100 / paths.size()));
    }
    set_status(L"完成：成功 " + std::to_wstring(paths.size() - failures) + L" 个，失败 " + std::to_wstring(failures) + L" 个", 100);
    if (failures > 0)
        MessageBoxW(g_main, failureDetails.c_str(), L"转换失败详情", MB_OK | MB_ICONWARNING);
    PostMessageW(g_main, WM_CONVERT_DONE, 0, 0);
}

void begin_conversion() {
    if (g_converting.exchange(true)) return;
    {
        std::lock_guard lock(g_pathsMutex);
        if (g_paths.empty()) { g_converting = false; MessageBoxW(g_main, L"请先添加要转换的音频文件。", L"还没有文件", MB_OK | MB_ICONINFORMATION); return; }
    }
    SendMessageW(g_progress, PBM_SETPOS, 0, 0);
    EnableWindow(GetDlgItem(g_main, IDC_CONVERT), FALSE);
    const int bitrate = selected_bitrate();
    std::thread([bitrate] {
        try {
            convert_all(bitrate);
        } catch (const std::exception& exception) {
            set_status(L"转换线程异常：" + widen(exception.what()));
            PostMessageW(g_main, WM_CONVERT_DONE, 0, 0);
        } catch (...) {
            set_status(L"转换线程发生未知异常");
            PostMessageW(g_main, WM_CONVERT_DONE, 0, 0);
        }
    }).detach();
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g_main = window;
        g_font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_SWISS, L"Microsoft YaHei UI");
        g_titleFont = CreateFontW(-32, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_SWISS, L"Microsoft YaHei UI");
        g_sectionFont = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_SWISS, L"Microsoft YaHei UI");
        g_backgroundBrush = CreateSolidBrush(RGB(245, 247, 250));
        g_controlBrush = CreateSolidBrush(RGB(255, 255, 255));
        g_panelBrush = CreateSolidBrush(RGB(255, 255, 255));

        auto make_static = [&](const wchar_t* text, int x, int y, int width, int height, int id, HFONT font = g_font) {
            const auto control = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, width, height, window, reinterpret_cast<HMENU>(id), nullptr, nullptr);
            apply_font(control, font);
            return control;
        };
        auto make_button = [&](const wchar_t* text, int x, int y, int width, int height, int id) {
            const auto control = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, y, width, height, window, reinterpret_cast<HMENU>(id), nullptr, nullptr);
            apply_font(control);
            return control;
        };

        make_static(L"音乐转 MP3", 44, 24, 420, 42, IDC_TITLE, g_titleFont);
        make_static(L"批量导入、后台转换，输出到你指定的目录", 46, 68, 600, 24, IDC_SUBTITLE);
        make_static(L"输入文件", 44, 112, 160, 24, IDC_INPUT_LABEL, g_sectionFont);
        g_files = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | LBS_NOTIFY, 44, 144, 856, 194, window, reinterpret_cast<HMENU>(IDC_FILES), nullptr, nullptr);
        apply_font(g_files);
        SendMessageW(g_files, LB_SETITEMHEIGHT, 0, 30);
        make_button(L"添加文件", 44, 356, 122, 38, IDC_ADD);
        make_button(L"添加文件夹", 178, 356, 132, 38, IDC_FOLDER);
        make_button(L"清空列表", 322, 356, 110, 38, IDC_CLEAR);

        make_static(L"输出设置", 44, 430, 160, 24, IDC_OUTPUT_GROUP_LABEL, g_sectionFont);
        make_static(L"输出目录", 44, 468, 82, 24, IDC_OUTPUT_LABEL, g_sectionFont);
        g_output = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"%USERPROFILE%\\Music\\MP3", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 142, 462, 596, 32, window, reinterpret_cast<HMENU>(IDC_OUTPUT), nullptr, nullptr);
        apply_font(g_output);
        DragAcceptFiles(g_output, TRUE);
        SetWindowSubclass(g_output, output_edit_subclass, 1, 0);
        make_button(L"选择目录", 752, 460, 148, 36, IDC_BROWSE);
        make_static(L"码率", 44, 512, 82, 24, IDC_BITRATE_LABEL, g_sectionFont);
        g_bitrate = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 142, 506, 170, 160, window, reinterpret_cast<HMENU>(IDC_BITRATE), nullptr, nullptr);
        apply_font(g_bitrate);
        for (const wchar_t* value : {L"128", L"192", L"256", L"320"}) SendMessageW(g_bitrate, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
        SendMessageW(g_bitrate, CB_SETCURSEL, 3, 0);
        make_static(L"kbps", 326, 512, 60, 24, IDC_KBPS_LABEL);
        make_button(L"开始转换", 656, 504, 244, 40, IDC_CONVERT);
        g_progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE, 44, 574, 856, 10, window, reinterpret_cast<HMENU>(IDC_PROGRESS), nullptr, nullptr);
        SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        g_status = CreateWindowW(L"STATIC", L"添加音乐文件后开始转换", WS_CHILD | WS_VISIBLE, 44, 596, 856, 28, window, reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);
        apply_font(g_progress);
        apply_font(g_status);
        DragAcceptFiles(window, TRUE);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, g_backgroundBrush);
        const auto layout = make_layout(client.right, client.bottom);
        draw_panel(dc, RECT{24, 96, layout.width - 24, layout.fileBottom});
        draw_panel(dc, RECT{24, layout.outputTop, layout.width - 24, layout.outputBottom});
        EndPaint(window, &paint);
        return 0;
    }
    case WM_SIZE:
        layout_controls(window);
        InvalidateRect(window, nullptr, FALSE);
        break;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = 798;
        limits->ptMinTrackSize.y = 609;
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_ADD: add_paths(choose_files(window)); break;
        case IDC_FOLDER: {
            const auto folders = choose_folder(window);
            if (!folders.empty()) add_folder_async(folders.front());
            break;
        }
        case IDC_CLEAR: {
            { std::lock_guard lock(g_pathsMutex); g_paths.clear(); }
            refresh_file_list();
            SendMessageW(g_progress, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_status, L"添加音乐文件后开始转换");
            break;
        }
        case IDC_BROWSE: { wchar_t current[MAX_PATH]{}; GetWindowTextW(g_output, current, MAX_PATH); SetWindowTextW(g_output, choose_output(window, current).c_str()); break; }
        case IDC_CONVERT: begin_conversion(); break;
        }
        break;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        POINT dropPoint{};
        if (DragQueryPoint(drop, &dropPoint)) {
            ClientToScreen(window, &dropPoint);
            const HWND target = WindowFromPoint(dropPoint);
            if (target == g_output || IsChild(g_output, target)) {
                handle_output_directory_drop(g_output, drop);
                break;
            }
        }
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<fs::path> paths;
        for (UINT i = 0; i < count; ++i) { wchar_t path[MAX_PATH * 4]{}; DragQueryFileW(drop, i, path, std::size(path)); paths.emplace_back(path); }
        DragFinish(drop);
        add_paths(paths);
        break;
    }
    case WM_CONVERT_STATUS: {
        auto* status = reinterpret_cast<StatusMessage*>(lParam);
        SetWindowTextW(g_status, status->text.c_str());
        if (status->progress >= 0) SendMessageW(g_progress, PBM_SETPOS, status->progress, 0);
        delete status;
        break;
    }
    case WM_CONVERT_DONE:
        g_converting = false;
        EnableWindow(GetDlgItem(window, IDC_CONVERT), TRUE);
        break;
    case WM_FILES_READY: {
        auto* files = reinterpret_cast<std::vector<fs::path>*>(lParam);
        merge_paths(*files);
        delete files;
        EnableWindow(GetDlgItem(window, IDC_FOLDER), TRUE);
        SetWindowTextW(g_status, L"准备就绪");
        break;
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlType == ODT_BUTTON) {
            draw_button(*item);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        const auto dc = reinterpret_cast<HDC>(wParam);
        const auto control = reinterpret_cast<HWND>(lParam);
        SetBkMode(dc, TRANSPARENT);
        const int id = GetDlgCtrlID(control);
        const bool onPanel = id == IDC_INPUT_LABEL || id == IDC_OUTPUT_GROUP_LABEL || id == IDC_OUTPUT_LABEL || id == IDC_BITRATE_LABEL || id == IDC_KBPS_LABEL;
        SetTextColor(dc, id == IDC_TITLE ? RGB(24, 92, 160) : RGB(71, 85, 105));
        return reinterpret_cast<LRESULT>(onPanel ? g_panelBrush : g_backgroundBrush);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        const auto dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, RGB(255, 255, 255));
        SetTextColor(dc, RGB(32, 40, 52));
        return reinterpret_cast<LRESULT>(g_controlBrush);
    }
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, g_backgroundBrush);
        return 1;
    }
    case WM_DESTROY:
        if (g_font) DeleteObject(g_font);
        if (g_titleFont) DeleteObject(g_titleFont);
        if (g_sectionFont) DeleteObject(g_sectionFont);
        if (g_backgroundBrush) DeleteObject(g_backgroundBrush);
        if (g_controlBrush) DeleteObject(g_controlBrush);
        if (g_panelBrush) DeleteObject(g_panelBrush);
        PostQuitMessage(0);
        break;
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\MusicToMp3Native.SingleInstance");
    if (!instanceMutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = nullptr;
        for (int attempt = 0; attempt < 20 && !existing; ++attempt) {
            existing = FindWindowW(L"MusicToMp3NativeWindow", nullptr);
            if (!existing) Sleep(50);
        }
        if (existing) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
            FlashWindow(existing, TRUE);
        }
        CloseHandle(instanceMutex);
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW klass{};
    klass.cbSize = sizeof(klass);
    klass.hInstance = instance;
    klass.lpfnWndProc = window_proc;
    klass.lpszClassName = L"MusicToMp3NativeWindow";
    klass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    HICON largeIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    HICON smallIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!largeIcon) largeIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_ICON1));
    if (!smallIcon) smallIcon = largeIcon;
    klass.hIcon = largeIcon;
    klass.hIconSm = smallIcon;
    klass.hbrBackground = nullptr;
    RegisterClassExW(&klass);
    HWND window = CreateWindowW(klass.lpszClassName, L"音乐转 MP3（原生版）", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 861, 672, nullptr, nullptr, instance, nullptr);
    if (window) {
        if (largeIcon) SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
        if (smallIcon) SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    }
    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    CoUninitialize();
    if (smallIcon && smallIcon != largeIcon) DestroyIcon(smallIcon);
    if (largeIcon) DestroyIcon(largeIcon);
    CloseHandle(instanceMutex);
    return static_cast<int>(message.wParam);
}
