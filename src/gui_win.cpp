#include <windows.h>
#include <shlobj.h>
#include <CommCtrl.h>

#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

namespace {
constexpr int idSource = 101;
constexpr int idBackup = 102;
constexpr int idArchive = 103;
constexpr int idRestore = 104;
constexpr int idPassword = 105;
constexpr int idOutput = 106;
constexpr int idBrowseSource = 107;
constexpr int idBrowseBackup = 108;
constexpr int idBrowseArchive = 109;
constexpr int idBrowseRestore = 110;
constexpr int idExtCombo = 111;
constexpr int idNameCombo = 112;
constexpr int idPathCombo = 113;
constexpr int idMaxSizeEdit = 114;
constexpr int idModifiedAfterCombo = 115;
constexpr int idModifiedBeforeCombo = 116;
constexpr int idBackupButton = 201;
constexpr int idPackButton = 202;
constexpr int idUnpackButton = 203;
constexpr int idRestoreButton = 204;
constexpr int idVerifyButton = 205;

HWND outputBox = nullptr;

std::wstring getText(HWND window, int id) {
    wchar_t buffer[1024]{};
    GetWindowTextW(GetDlgItem(window, id), buffer, 1024);
    return buffer;
}

void setOutput(const std::wstring& text) {
    SetWindowTextW(outputBox, text.c_str());
}

std::wstring quote(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

std::wstring executablePath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring fullPath = path;
    const auto slash = fullPath.find_last_of(L"\\/");
    const auto dir = slash == std::wstring::npos ? L"." : fullPath.substr(0, slash);
    return dir + L"\\sbm.exe";
}

void browseFolder(HWND window, int editId) {
    BROWSEINFOW bi{};
    bi.hwndOwner = window;
    bi.lpszTitle = L"Select a folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    const auto pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t path[MAX_PATH]{};
        SHGetPathFromIDListW(pidl, path);
        CoTaskMemFree(pidl);
        SetWindowTextW(GetDlgItem(window, editId), path);
    }
}

void browseFile(HWND window, int editId) {
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window;
    ofn.lpstrFilter = L"Simple Backup Archive (*.sba)\0*.sba\0All Files (*.*)\0*.*\0";
    ofn.lpstrDefExt = L"sba";
    wchar_t path[MAX_PATH]{};
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetSaveFileNameW(&ofn)) {
        SetWindowTextW(GetDlgItem(window, editId), path);
    }
}

std::wstring getComboText(HWND window, int comboId) {
    HWND combo = GetDlgItem(window, comboId);
    const int idx = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (idx < 0) return L"";
    wchar_t buffer[256]{};
    SendMessageW(combo, CB_GETLBTEXT, idx, reinterpret_cast<LPARAM>(buffer));
    return buffer;
}

void addComboStrings(HWND window, int comboId, const std::vector<const wchar_t*>& items) {
    HWND combo = GetDlgItem(window, comboId);
    for (const auto* item : items) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

void runCommand(const std::wstring& command) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) {
        setOutput(L"Failed to create output pipe.");
        return;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullptr;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;

    const auto fullCommand = quote(executablePath()) + L" " + command;
    std::vector<wchar_t> mutableCommand(fullCommand.begin(), fullCommand.end());
    mutableCommand.push_back(L'\0');

    PROCESS_INFORMATION process{};
    const BOOL launched = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr,
                                         TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                         &startup, &process);
    CloseHandle(writeEnd);

    if (!launched) {
        CloseHandle(readEnd);
        setOutput(L"Failed to start sbm.exe. Please build the project first.");
        return;
    }

    std::string bytes;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        bytes.append(buffer, read);
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    CloseHandle(readEnd);

    if (bytes.empty()) {
        setOutput(L"(command produced no output)");
        return;
    }

    int wideSize = MultiByteToWideChar(CP_UTF8, 0, bytes.data(),
                                        static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring output;
    const UINT codePage = wideSize > 0 ? CP_UTF8 : CP_ACP;
    if (wideSize <= 0) {
        wideSize = MultiByteToWideChar(CP_ACP, 0, bytes.data(),
                                       static_cast<int>(bytes.size()), nullptr, 0);
    }
    output.resize(wideSize);
    MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()),
                        output.data(), wideSize);
    setOutput(output);
}

void addLabel(HWND window, const wchar_t* text, int x, int y) {
    CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
                  x, y, 80, 22, window, nullptr, nullptr, nullptr);
}

void addEdit(HWND window, int id, int x, int y, int width) {
    CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                  x, y, width, 24, window, reinterpret_cast<HMENU>(id), nullptr, nullptr);
}

void addBrowseButton(HWND window, int id, int x, int y) {
    CreateWindowW(L"BUTTON", L"...", WS_CHILD | WS_VISIBLE,
                  x, y, 30, 24, window, reinterpret_cast<HMENU>(id), nullptr, nullptr);
}

void addButton(HWND window, int id, const wchar_t* text, int x, int y) {
    CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE,
                  x, y, 100, 30, window, reinterpret_cast<HMENU>(id), nullptr, nullptr);
}

void addCombo(HWND window, int id, int x, int y, int width) {
    CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                  x, y, width, 200, window, reinterpret_cast<HMENU>(id), nullptr, nullptr);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        // --- Row 0 (y=20): Source ---
        addLabel(window, L"Source", 20, 22);
        addEdit(window, idSource, 110, 20, 450);
        addBrowseButton(window, idBrowseSource, 565, 20);

        // --- Row 1 (y=56): Backup ---
        addLabel(window, L"Backup", 20, 58);
        addEdit(window, idBackup, 110, 56, 450);
        addBrowseButton(window, idBrowseBackup, 565, 56);

        // --- Row 2 (y=92): Archive ---
        addLabel(window, L"Archive", 20, 94);
        addEdit(window, idArchive, 110, 92, 450);
        addBrowseButton(window, idBrowseArchive, 565, 92);

        // --- Row 3 (y=128): Restore ---
        addLabel(window, L"Restore", 20, 130);
        addEdit(window, idRestore, 110, 128, 450);
        addBrowseButton(window, idBrowseRestore, 565, 128);

        // --- Row 4 (y=164): Password ---
        addLabel(window, L"Password", 20, 166);
        addEdit(window, idPassword, 110, 164, 160);

        // --- Row 5 (y=200): Filter labels ---
        {
            const auto filterY = 200;
            CreateWindowW(L"STATIC", L"Ext:", WS_CHILD | WS_VISIBLE,
                          20, filterY, 60, 22, window, nullptr, nullptr, nullptr);
            addCombo(window, idExtCombo, 60, filterY, 100);

            CreateWindowW(L"STATIC", L"Name:", WS_CHILD | WS_VISIBLE,
                          170, filterY, 60, 22, window, nullptr, nullptr, nullptr);
            addCombo(window, idNameCombo, 215, filterY, 100);

            CreateWindowW(L"STATIC", L"Path:", WS_CHILD | WS_VISIBLE,
                          325, filterY, 60, 22, window, nullptr, nullptr, nullptr);
            addCombo(window, idPathCombo, 370, filterY, 100);

            CreateWindowW(L"STATIC", L"MaxSize(B):", WS_CHILD | WS_VISIBLE,
                          480, filterY, 80, 22, window, nullptr, nullptr, nullptr);
            addEdit(window, idMaxSizeEdit, 560, filterY, 100);

            CreateWindowW(L"STATIC", L"After:", WS_CHILD | WS_VISIBLE,
                          20, filterY + 28, 60, 22, window, nullptr, nullptr, nullptr);
            addCombo(window, idModifiedAfterCombo, 60, filterY + 28, 120);

            CreateWindowW(L"STATIC", L"Before:", WS_CHILD | WS_VISIBLE,
                          190, filterY + 28, 60, 22, window, nullptr, nullptr, nullptr);
            addCombo(window, idModifiedBeforeCombo, 245, filterY + 28, 120);
        }

        // Populate filter combos
        addComboStrings(window, idExtCombo, {
            L"(none)",
            L".txt",
            L".cpp",
            L".pdf",
            L".txt,.cpp",
            L".txt,.pdf",
            L".cpp,.h",
            L".txt,.cpp,.h,.pdf"
        });

        addComboStrings(window, idNameCombo, {
            L"(none)",
            L"report",
            L"readme",
            L"main",
            L"utils",
            L"notes"
        });

        addComboStrings(window, idPathCombo, {
            L"(none)",
            L"docs",
            L"src",
            L"logs",
            L"images",
            L"config"
        });

        addComboStrings(window, idModifiedAfterCombo, {
            L"(none)",
            L"2024-01-01",
            L"2024-06-01",
            L"2025-01-01",
            L"2025-06-01",
            L"2026-01-01"
        });

        addComboStrings(window, idModifiedBeforeCombo, {
            L"(none)",
            L"2024-12-31",
            L"2025-06-30",
            L"2025-12-31",
            L"2026-06-30",
            L"2026-12-31"
        });

        // --- Buttons ---
        addButton(window, idBackupButton, L"Backup", 20, 270);
        addButton(window, idPackButton, L"Pack", 140, 270);
        addButton(window, idUnpackButton, L"Unpack", 260, 270);
        addButton(window, idRestoreButton, L"Restore", 380, 270);
        addButton(window, idVerifyButton, L"Verify", 500, 270);

        CreateWindowW(L"STATIC", L"Compression: Zlib (always on)", WS_CHILD | WS_VISIBLE,
                      20, 308, 250, 18, window, nullptr, nullptr, nullptr);

        // --- Output ---
        outputBox = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER |
                                      ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
                                  20, 330, 650, 160, window,
                                  reinterpret_cast<HMENU>(idOutput), nullptr, nullptr);
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const auto source = getText(window, idSource);
        const auto backup = getText(window, idBackup);
        const auto archive = getText(window, idArchive);
        const auto restore = getText(window, idRestore);
        const auto password = getText(window, idPassword);

        // Browse buttons
        if (id == idBrowseSource) { browseFolder(window, idSource); return 0; }
        if (id == idBrowseBackup) { browseFolder(window, idBackup); return 0; }
        if (id == idBrowseArchive) { browseFile(window, idArchive); return 0; }
        if (id == idBrowseRestore) { browseFolder(window, idRestore); return 0; }

        if (id == idBackupButton) {
            std::wstring cmd = L"backup " + quote(source) + L" " + quote(backup) + L" --overwrite";

            const auto ext = getComboText(window, idExtCombo);
            if (!ext.empty() && ext != L"(none)")
                cmd += L" --ext=" + ext;

            const auto name = getComboText(window, idNameCombo);
            if (!name.empty() && name != L"(none)")
                cmd += L" --name-contains=" + name;

            const auto path = getComboText(window, idPathCombo);
            if (!path.empty() && path != L"(none)")
                cmd += L" --path-contains=" + path;

            const auto maxSize = getText(window, idMaxSizeEdit);
            if (!maxSize.empty())
                cmd += L" --max-size=" + maxSize;

            const auto after = getComboText(window, idModifiedAfterCombo);
            if (!after.empty() && after != L"(none)")
                cmd += L" --modified-after=" + after;

            const auto before = getComboText(window, idModifiedBeforeCombo);
            if (!before.empty() && before != L"(none)")
                cmd += L" --modified-before=" + before;

            runCommand(cmd);
        } else if (id == idPackButton) {
            auto command = L"pack " + quote(backup) + L" " + quote(archive);
            if (!password.empty()) command += L" --password=" + password;
            runCommand(command);
        } else if (id == idUnpackButton) {
            auto command = L"unpack " + quote(archive) + L" " + quote(backup);
            if (!password.empty()) command += L" --password=" + password;
            runCommand(command);
        } else if (id == idRestoreButton) {
            runCommand(L"restore " + quote(backup) + L" " + quote(restore));
        } else if (id == idVerifyButton) {
            runCommand(L"verify " + quote(backup));
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    const wchar_t className[] = L"SimpleBackupManagerGui";

    // Initialize common controls
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND window = CreateWindowExW(0, className, L"Simple Backup Manager - Native GUI",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  720, 540, nullptr, nullptr, instance, nullptr);
    if (!window) {
        return 1;
    }

    ShowWindow(window, showCommand);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
