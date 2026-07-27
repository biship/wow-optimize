#include "saved_vars_backup.h"

namespace SavedVarsBackup {

// Recursively copy every *.lua under `dir` to <file>.bak. WTF/Account holds only
// SavedVariables .lua config files, so this backs those up and nothing else.
// Returns how many files were copied. Depth-capped as a safety belt.
static int BackupDir(const std::string& dir, int depth) {
    if (depth > 8) return 0;
    int count = 0;
    WIN32_FIND_DATAA fd;
    std::string pattern = dir + "\\*";
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.cFileName[0] == '.') continue;   // skip . and ..
        std::string full = dir + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            count += BackupDir(full, depth + 1);
        } else {
            size_t len = strlen(fd.cFileName);
            if (len > 4 && _stricmp(fd.cFileName + len - 4, ".lua") == 0) {
                if (CopyFileA(full.c_str(), (full + ".bak").c_str(), FALSE)) count++;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return count;
}

extern "C" void Log(const char* fmt, ...);

static DWORD WINAPI BackupThread(LPVOID) {
    char exePath[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exePath, MAX_PATH)) return 0;
    char* slash = strrchr(exePath, '\\');
    if (!slash) return 0;
    *slash = 0;   // strip Wow.exe -> game directory
    std::string accountDir = std::string(exePath) + "\\WTF\\Account";

    int n = BackupDir(accountDir, 0);
    Log("[SavedVarsBackup] Startup backup: %d SavedVariables .lua -> .bak", n);
    return 0;
}

bool Init() {
    // Back up the last-good SavedVariables once, at startup, before this session
    // can write (and possibly corrupt) them. Runs on a background thread so it
    // never delays load; only ever copies existing files.
    HANDLE h = CreateThread(nullptr, 0, BackupThread, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    return true;
}

void Shutdown() {
    // No-op
}

void CreateBackup(const std::string& filePath) {
    if (filePath.empty()) return;
    // Back up a single WTF SavedVariables .lua on demand.
    if (filePath.find("WTF") != std::string::npos && filePath.find(".lua") != std::string::npos) {
        CopyFileA(filePath.c_str(), (filePath + ".bak").c_str(), FALSE);
    }
}

} // namespace SavedVarsBackup
