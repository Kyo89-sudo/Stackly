// StacklySetup - installer/disinstallatore di Stackly (Win32 C++17, nessuna dipendenza)
// - installazione per utente in %LOCALAPPDATA%\Programs\Stackly: niente UAC
// - collegamenti in Start e (opzionale) sul desktop
// - voce in "App installate" con disinstallazione
// Opzioni: /S = installazione silenziosa, /D=cartella, /uninstall = disinstalla
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <iterator>
#include <string>
#include <vector>
#include "../src/resource.h"

#define IDR_PAYLOAD 200
#define IDI_SETUP   201

static const wchar_t* kUninstKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Stackly";
// Lingua dell'installer: italiano se Windows e' in italiano, altrimenti inglese
static const bool g_en = PRIMARYLANGID(GetUserDefaultUILanguage()) != LANG_ITALIAN;
static const wchar_t* T(const wchar_t* it, const wchar_t* en) { return g_en ? en : it; }
#define kTitle T(L"Installazione di Stackly", L"Stackly Setup")
static HINSTANCE g_inst;
static HWND g_path, g_browse, g_desk, g_start, g_launch, g_ok, g_cancel, g_lbl, g_note;
static HFONT g_font, g_big, g_small;
static UINT g_dpi = 96;
static int S(int v) { return MulDiv(v, (int)g_dpi, 96); }

// ---------------------------------------------------------------------------
static std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) { r = p; CoTaskMemFree(p); }
    return r;
}
static std::wstring DefaultDir() { return KnownFolder(FOLDERID_UserProgramFiles) + L"\\Stackly"; }
static std::wstring SelfPath() {
    wchar_t b[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, b, (DWORD)std::size(b));
    return b;
}
static std::wstring Trim(std::wstring s) {
    while (!s.empty() && (iswspace(s.back()) || s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    size_t a = 0;
    while (a < s.size() && iswspace(s[a])) ++a;
    return s.substr(a);
}
static std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}
static void DeleteTree(const std::wstring& dir) {
    std::wstring from = dir;
    from += L'\0';
    from += L'\0';
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NO_UI;
    SHFileOperationW(&op);
}

// Chiude Stackly se e' aperto (con conferma dell'app se ci sono download in corso)
static bool CloseRunningApp(HWND owner, bool silent) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        HWND h = FindWindowW(L"StacklyMainWnd", nullptr);
        if (!h) return true;
        if (!silent && attempt == 0 &&
            MessageBoxW(owner, T(L"Stackly \u00e8 aperto. Chiuderlo per continuare?", L"Stackly is running. Close it to continue?"), kTitle, MB_ICONQUESTION | MB_OKCANCEL) != IDOK)
            return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
        PostMessageW(h, WM_COMMAND, ID_TRAY_EXIT, 0);
        if (proc) {
            DWORD w = WaitForSingleObject(proc, 15000);
            CloseHandle(proc);
            if (w == WAIT_OBJECT_0) return true;
        } else {
            Sleep(2000);
        }
    }
    if (!silent) MessageBoxW(owner, T(L"Chiudi Stackly (anche dall'area di notifica) e riprova.", L"Close Stackly (including from the notification area) and try again."), kTitle, MB_ICONWARNING);
    return FindWindowW(L"StacklyMainWnd", nullptr) == nullptr;
}

static bool MakeShortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& dir, const std::wstring& desc) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&sl)))) return false;
    sl->SetPath(target.c_str());
    sl->SetWorkingDirectory(dir.c_str());
    sl->SetDescription(desc.c_str());
    sl->SetIconLocation(target.c_str(), 0);
    IPersistFile* pf = nullptr;
    bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_PPV_ARGS(&pf)))) {
        ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
        pf->Release();
    }
    sl->Release();
    return ok;
}

static bool WritePayload(const std::wstring& dest, std::wstring& err) {
    HRSRC r = FindResourceW(g_inst, MAKEINTRESOURCEW(IDR_PAYLOAD), RT_RCDATA);
    HGLOBAL g = r ? LoadResource(g_inst, r) : nullptr;
    const void* data = g ? LockResource(g) : nullptr;
    DWORD size = r ? SizeofResource(g_inst, r) : 0;
    if (!data || !size) { err = T(L"pacchetto interno mancante", L"embedded package missing"); return false; }
    std::wstring tmp = dest + L".new";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = T(L"impossibile scrivere ", L"cannot write ") + tmp; return false; }
    DWORD w = 0;
    BOOL ok = WriteFile(f, data, size, &w, nullptr) && w == size;
    FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        err = T(L"impossibile scrivere ", L"cannot write ") + dest;
        return false;
    }
    return true;
}

static DWORD DirSizeKB(const std::wstring& dir) {
    ULONGLONG total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            total += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return (DWORD)(total / 1024);
}

static void RegStr(HKEY k, const wchar_t* name, const std::wstring& v) {
    RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}
static void RegDw(HKEY k, const wchar_t* name, DWORD v) {
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
}

static bool DoInstall(const std::wstring& dirIn, bool desk, bool start, std::wstring& err) {
    std::wstring dir = Trim(dirIn);
    if (dir.empty()) { err = T(L"cartella non valida", L"invalid folder"); return false; }
    int rc = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (rc != ERROR_SUCCESS && rc != ERROR_ALREADY_EXISTS && rc != ERROR_FILE_EXISTS) { err = T(L"impossibile creare ", L"cannot create ") + dir; return false; }
    std::wstring exe = dir + L"\\Stackly.exe";
    std::wstring uninst = dir + L"\\uninstall.exe";
    if (!WritePayload(exe, err)) return false;
    std::wstring self = SelfPath();
    if (_wcsicmp(self.c_str(), uninst.c_str()) != 0 && !CopyFileW(self.c_str(), uninst.c_str(), FALSE)) {
        err = T(L"impossibile copiare il disinstallatore", L"cannot copy the uninstaller");
        return false;
    }
    std::wstring programs = KnownFolder(FOLDERID_Programs);
    if (start && !programs.empty()) MakeShortcut(programs + L"\\Stackly.lnk", exe, dir, T(L"Scarica video e audio con yt-dlp", L"Download video and audio with yt-dlp"));
    std::wstring desktop = KnownFolder(FOLDERID_Desktop);
    if (desk && !desktop.empty()) MakeShortcut(desktop + L"\\Stackly.lnk", exe, dir, T(L"Scarica video e audio con yt-dlp", L"Download video and audio with yt-dlp"));
    else if (!desktop.empty()) DeleteFileW((desktop + L"\\Stackly.lnk").c_str());

    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        RegStr(k, L"DisplayName", L"Stackly");
        RegStr(k, L"DisplayVersion", STACKLY_VERSION_W);
        RegStr(k, L"Publisher", L"Stackly");
        RegStr(k, L"DisplayIcon", exe + L",0");
        RegStr(k, L"InstallLocation", dir);
        RegStr(k, L"UninstallString", L"\"" + uninst + L"\" /uninstall");
        RegStr(k, L"QuietUninstallString", L"\"" + uninst + L"\" /uninstall /S");
        RegDw(k, L"NoModify", 1);
        RegDw(k, L"NoRepair", 1);
        RegDw(k, L"EstimatedSize", DirSizeKB(dir));
        RegCloseKey(k);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

// ---------------------------------------------------------------------------
// Disinstallazione
// ---------------------------------------------------------------------------
static std::wstring InstalledDir() {
    wchar_t b[MAX_PATH * 2];
    DWORD sz = sizeof(b);
    if (RegGetValueW(HKEY_CURRENT_USER, kUninstKey, L"InstallLocation", RRF_RT_REG_SZ, nullptr, b, &sz) == ERROR_SUCCESS)
        return b;
    std::wstring self = SelfPath();
    return self.substr(0, self.find_last_of(L'\\'));
}

static int Uninstall(bool silent) {
    std::wstring dir = InstalledDir();
    BOOL purge = FALSE;
    if (!silent) {
        TASKDIALOGCONFIG tc{};
        tc.cbSize = sizeof(tc);
        tc.hInstance = g_inst;
        tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        tc.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
        tc.pszWindowTitle = T(L"Disinstalla Stackly", L"Uninstall Stackly");
        tc.pszMainIcon = TD_WARNING_ICON;
        tc.pszMainInstruction = T(L"Disinstallare Stackly?", L"Uninstall Stackly?");
        tc.pszContent = T(L"I file che hai scaricato non vengono toccati.", L"The files you downloaded are not touched.");
        tc.pszVerificationText = T(L"Elimina anche impostazioni e componenti scaricati (yt-dlp, ffmpeg, Deno)", L"Also delete settings and downloaded components (yt-dlp, ffmpeg, Deno)");
        tc.nDefaultButton = IDNO;
        int btn = 0;
        if (FAILED(TaskDialogIndirect(&tc, &btn, nullptr, &purge)) || btn != IDYES) return 1;
    }
    if (!CloseRunningApp(nullptr, silent)) return 2;

    std::wstring programs = KnownFolder(FOLDERID_Programs), desktop = KnownFolder(FOLDERID_Desktop);
    if (!programs.empty()) DeleteFileW((programs + L"\\Stackly.lnk").c_str());
    if (!desktop.empty()) DeleteFileW((desktop + L"\\Stackly.lnk").c_str());
    DeleteFileW((dir + L"\\Stackly.exe").c_str());
    if (purge) {
        DeleteTree(dir + L"\\bin");
        DeleteFileW((dir + L"\\Stackly.ini").c_str());
        std::wstring local = KnownFolder(FOLDERID_LocalAppData);
        if (!local.empty()) DeleteTree(local + L"\\Stackly");
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, kUninstKey);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    // uninstall.exe non puo' cancellare se stesso: una copia temporanea finisce il lavoro
    wchar_t tmpDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpDir);
    std::wstring tmp = std::wstring(tmpDir) + L"stackly_uninst_" + std::to_wstring(GetCurrentProcessId()) + L".exe";
    if (CopyFileW(SelfPath().c_str(), tmp.c_str(), FALSE)) {
        std::wstring cmd = L"\"" + tmp + L"\" /finish " + std::to_wstring(GetCurrentProcessId()) + L" \"" + dir + L"\"";
        STARTUPINFOW si{}; si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        if (CreateProcessW(tmp.c_str(), buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, tmpDir, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    if (!silent) MessageBoxW(nullptr, T(L"Stackly \u00e8 stato disinstallato.", L"Stackly has been uninstalled."), T(L"Disinstalla Stackly", L"Uninstall Stackly"), MB_ICONINFORMATION);
    return 0;
}

// Copia temporanea: attende la fine del disinstallatore, rimuove la cartella e poi se stessa
static int Finish(DWORD pid, const std::wstring& dir) {
    if (HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, pid)) { WaitForSingleObject(p, 30000); CloseHandle(p); }
    for (int i = 0; i < 20; ++i) {
        DeleteFileW((dir + L"\\uninstall.exe").c_str());
        if (GetFileAttributesW((dir + L"\\uninstall.exe").c_str()) == INVALID_FILE_ATTRIBUTES) break;
        Sleep(250);
    }
    RemoveDirectoryW(dir.c_str());   // solo se vuota (impostazioni e bin restano se non si e' scelto di eliminarli)
    wchar_t cmdExe[MAX_PATH];
    GetSystemDirectoryW(cmdExe, MAX_PATH);
    std::wstring self = SelfPath();
    std::wstring cmd = std::wstring(L"\"") + cmdExe + L"\\cmd.exe\" /c ping -n 3 127.0.0.1 >nul & del /f /q \"" + self + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Finestra di installazione
// ---------------------------------------------------------------------------
enum { ID_PATH = 10, ID_BROWSE, ID_DESK, ID_START, ID_LAUNCH, ID_INSTALL, ID_CANCEL };

static HFONT MkFont(int pt, int weight) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(pt, (int)g_dpi, 72);
    lf.lfWeight = weight;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

static void Layout(HWND h) {
    RECT rc;
    GetClientRect(h, &rc);
    int m = S(24), W = rc.right, y = S(110);
    MoveWindow(g_lbl, m, y, W - 2 * m, S(20), TRUE); y += S(24);
    MoveWindow(g_path, m, y, W - 2 * m - S(110), S(28), TRUE);
    MoveWindow(g_browse, W - m - S(100), y, S(100), S(28), TRUE); y += S(44);
    MoveWindow(g_start, m, y, W - 2 * m, S(22), TRUE); y += S(26);
    MoveWindow(g_desk, m, y, W - 2 * m, S(22), TRUE); y += S(26);
    MoveWindow(g_launch, m, y, W - 2 * m, S(22), TRUE); y += S(34);
    MoveWindow(g_note, m, y, W - 2 * m, S(36), TRUE);
    int by = rc.bottom - m - S(34);
    MoveWindow(g_ok, W - m - S(120), by, S(120), S(34), TRUE);
    MoveWindow(g_cancel, W - m - S(120) - S(10) - S(100), by, S(100), S(34), TRUE);
}

static void PaintHeader(HDC dc, const RECT& rc) {
    RECT hr{ 0, 0, rc.right, S(92) };
    TRIVERTEX v[2] = { { 0, 0, 0xCE00, 0x3A00, 0xE200, 0xFF00 }, { hr.right, hr.bottom, 0xE200, 0x1E00, 0x5200, 0xFF00 } };
    GRADIENT_RECT gr{ 0, 1 };
    GradientFill(dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H);
    HICON ic = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_SETUP), IMAGE_ICON, S(56), S(56), 0);
    DrawIconEx(dc, S(24), S(18), ic, S(56), S(56), 0, nullptr, DI_NORMAL);
    DestroyIcon(ic);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    HGDIOBJ old = SelectObject(dc, g_big);
    RECT t{ S(96), S(16), rc.right - S(20), S(52) };
    DrawTextW(dc, L"Stackly", -1, &t, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, g_font);
    RECT t2{ S(96), S(50), rc.right - S(20), S(76) };
    std::wstring sub = std::wstring(T(L"Installazione della versione ", L"Installing version ")) + STACKLY_VERSION_W;
    DrawTextW(dc, sub.c_str(), -1, &t2, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, old);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD ex = 0) {
            HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, h, (HMENU)(INT_PTR)id, g_inst, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)g_font, FALSE);
            return c;
        };
        g_lbl = mk(WC_STATICW, T(L"Cartella di installazione:", L"Install folder:"), 0, 0);
        g_path = mk(WC_EDITW, DefaultDir().c_str(), WS_TABSTOP | ES_AUTOHSCROLL, ID_PATH, WS_EX_CLIENTEDGE);
        g_browse = mk(WC_BUTTONW, T(L"Sfoglia...", L"Browse..."), WS_TABSTOP, ID_BROWSE);
        g_start = mk(WC_BUTTONW, T(L"Collegamento nel menu Start", L"Start menu shortcut"), WS_TABSTOP | BS_AUTOCHECKBOX, ID_START);
        g_desk = mk(WC_BUTTONW, T(L"Collegamento sul desktop", L"Desktop shortcut"), WS_TABSTOP | BS_AUTOCHECKBOX, ID_DESK);
        g_launch = mk(WC_BUTTONW, T(L"Avvia Stackly al termine", L"Launch Stackly when done"), WS_TABSTOP | BS_AUTOCHECKBOX, ID_LAUNCH);
        g_note = mk(WC_STATICW, T(L"Installazione solo per il tuo utente: non servono permessi di amministratore. "
                                  L"Al primo avvio Stackly scarica i componenti necessari (yt-dlp, ffmpeg, Deno).",
                                  L"Installs for your user only: no administrator rights needed. "
                                  L"On first launch Stackly downloads the components it needs (yt-dlp, ffmpeg, Deno)."), 0, 0);
        SendMessageW(g_note, WM_SETFONT, (WPARAM)g_small, FALSE);
        g_cancel = mk(WC_BUTTONW, T(L"Annulla", L"Cancel"), WS_TABSTOP, ID_CANCEL);
        g_ok = mk(WC_BUTTONW, T(L"Installa", L"Install"), WS_TABSTOP | BS_DEFPUSHBUTTON, ID_INSTALL);
        for (HWND c : { g_start, g_desk, g_launch }) SendMessageW(c, BM_SETCHECK, BST_CHECKED, 0);
        Layout(h);
        return 0;
    }
    case WM_SIZE:
        Layout(h);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, (HWND)lp == g_note ? RGB(100, 100, 110) : GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)wp, &rc, GetSysColorBrush(COLOR_WINDOW));
        PaintHeader((HDC)wp, rc);
        return 1;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
        case ID_INSTALL: {
            std::wstring dir = Trim(GetText(g_path)), err;
            if (!CloseRunningApp(h, false)) return 0;
            EnableWindow(g_ok, FALSE);
            SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            bool ok = DoInstall(dir, SendMessageW(g_desk, BM_GETCHECK, 0, 0) == BST_CHECKED,
                                SendMessageW(g_start, BM_GETCHECK, 0, 0) == BST_CHECKED, err);
            EnableWindow(g_ok, TRUE);
            if (!ok) {
                MessageBoxW(h, (T(L"Installazione non riuscita: ", L"Installation failed: ") + err).c_str(), kTitle, MB_ICONERROR);
                return 0;
            }
            if (SendMessageW(g_launch, BM_GETCHECK, 0, 0) == BST_CHECKED)
                ShellExecuteW(nullptr, L"open", (dir + L"\\Stackly.exe").c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
            else
                MessageBoxW(h, T(L"Stackly \u00e8 stato installato.", L"Stackly has been installed."), kTitle, MB_ICONINFORMATION);
            DestroyWindow(h);
            return 0;
        }
        case ID_BROWSE: {
            IFileOpenDialog* dlg = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) {
                DWORD o = 0;
                dlg->GetOptions(&o);
                dlg->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                if (SUCCEEDED(dlg->Show(h))) {
                    IShellItem* si = nullptr;
                    if (SUCCEEDED(dlg->GetResult(&si))) {
                        PWSTR p = nullptr;
                        if (SUCCEEDED(si->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                            std::wstring d = p;
                            CoTaskMemFree(p);
                            // installa sempre in una sottocartella "Stackly"
                            if (d.size() < 8 || _wcsicmp(d.c_str() + d.size() - 7, L"Stackly") != 0) d = Trim(d) + L"\\Stackly";
                            SetWindowTextW(g_path, d.c_str());
                        }
                        si->Release();
                    }
                }
                dlg->Release();
            }
            return 0;
        }
        case IDCANCEL:
        case ID_CANCEL:
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    g_inst = inst;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool silent = false, uninstall = false;
    std::wstring dir = DefaultDir();
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (!_wcsicmp(a.c_str(), L"/S")) silent = true;
        else if (!_wcsicmp(a.c_str(), L"/uninstall")) uninstall = true;
        else if (!_wcsnicmp(a.c_str(), L"/D=", 3)) dir = a.substr(3);
        else if (!_wcsicmp(a.c_str(), L"/finish") && i + 2 < argc) {
            int rc = Finish((DWORD)_wtoi(argv[i + 1]), argv[i + 2]);
            LocalFree(argv);
            return rc;
        }
    }
    LocalFree(argv);

    // Il disinstallatore copiato nella cartella si avvia sempre in modalita' disinstallazione
    std::wstring self = SelfPath();
    std::wstring base = self.substr(self.find_last_of(L'\\') + 1);
    if (!_wcsicmp(base.c_str(), L"uninstall.exe")) uninstall = true;

    if (uninstall) return Uninstall(silent);
    if (silent) {
        std::wstring err;
        if (!CloseRunningApp(nullptr, true)) return 2;
        return DoInstall(dir, true, true, err) ? 0 : 1;
    }

    HDC dc = GetDC(nullptr);
    g_dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    g_font = MkFont(10, FW_NORMAL);
    g_small = MkFont(9, FW_NORMAL);
    g_big = MkFont(20, FW_SEMIBOLD);

    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_SETUP));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"StacklySetupWnd";
    RegisterClassExW(&wc);
    RECT r{ 0, 0, S(560), S(420) };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    AdjustWindowRect(&r, style, FALSE);
    int w = r.right - r.left, hgt = r.bottom - r.top;
    HWND h = CreateWindowExW(0, wc.lpszClassName, kTitle, style, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                             (GetSystemMetrics(SM_CYSCREEN) - hgt) / 2, w, hgt, nullptr, nullptr, inst, nullptr);
    ShowWindow(h, show);
    UpdateWindow(h);
    SetFocus(g_ok);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(h, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    CoUninitialize();
    return 0;
}
