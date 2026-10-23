// Stackly - tipi, stato globale, utilita', impostazioni
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <dwmapi.h>
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>

#include <atomic>
#include <climits>
#include <cwctype>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "resource.h"
#include "json.h"
#include "lang.h"
#include "net.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#endif

// ---------------------------------------------------------------------------
// Costanti e tipi
// ---------------------------------------------------------------------------
static const wchar_t* kAppName = L"Stackly";
static const wchar_t* kVersion = STACKLY_VERSION_W;
static const wchar_t* kClass   = L"StacklyMainWnd";
static const wchar_t* kRepoUrl   = L"https://github.com/Kyo89-sudo/Stackly";
static const wchar_t* kIssuesUrl = L"https://github.com/Kyo89-sudo/Stackly/issues";
static const wchar_t* kRepoShort = L"github.com/Kyo89-sudo/Stackly";

enum : UINT {
    WM_APP_LINE       = WM_APP + 1,   // wParam = id, lParam = std::wstring*
    WM_APP_DONE       = WM_APP + 2,   // wParam = id, lParam = exit code
    WM_APP_TOOLSTATUS = WM_APP + 3,   // lParam = std::wstring*
    WM_APP_TOOLDONE   = WM_APP + 4,   // lParam = std::wstring*
    WM_APP_FIRSTRUN   = WM_APP + 5,
    WM_APP_FALLBACK   = WM_APP + 6,   // wParam = id: il motore nativo passa la mano a yt-dlp
    WM_APP_THUMB      = WM_APP + 7,   // lParam = ThumbMsg*
    WM_APP_ANALYZED   = WM_APP + 8,   // lParam = AnalyzeResult*
    WM_APP_TRAY       = WM_APP + 9,
    WM_APP_ABOUTVER   = WM_APP + 10,  // lParam = AboutVers* (versioni dei componenti, alla finestra Informazioni)
};

static const UINT_PTR kRepaintTimer = 1;
static const UINT kRepaintMs = 150;
static const int kGlobalConnections = 24;   // budget totale di connessioni HTTP simultanee

enum class St { Queued, Starting, Downloading, Processing, Done, Error, Cancelled };

// Cosa scaricare (i valori restano quelli salvati nelle versioni precedenti: 0, 1, 2)
enum DlKind { DK_VIDEO = 0, DK_AUDIO = 1, DK_MANUAL = 2, DK_VIDEOONLY = 3, DK_SUBS = 4 };

// N_() marca un testo da tradurre senza tradurlo subito (tabelle costanti create prima di sapere la lingua)
#define N_(s) s

struct Choice {
    const wchar_t* label_;   // testo nel menu
    const wchar_t* value_;   // testo mostrato nel campo
    const wchar_t* hint_;    // spiegazione breve (menu)
    const wchar_t* label() const { return TR(label_); }
    const wchar_t* value() const { return TR(value_); }
    const wchar_t* hint() const { return TR(hint_); }
};

// Indicizzato per DlKind; l'ordine nel menu e' in kKindOrder
static const Choice kKinds[] = {
    { N_(L"Video + audio"), N_(L"Video + audio"), N_(L"il video completo, pronto da guardare") },
    { N_(L"Solo audio"), N_(L"Solo audio"), N_(L"solo la traccia audio (musica, podcast)") },
    { N_(L"Scegli ogni volta"), N_(L"Scegli ogni volta"), N_(L"analizza il link e mostra tutti i formati") },
    { N_(L"Solo video"), N_(L"Solo video"), N_(L"immagini senza audio (montaggio, archivio)") },
    { N_(L"Solo sottotitoli"), N_(L"Solo sottotitoli"), N_(L"niente video n\u00e9 audio, solo i file dei sottotitoli") },
};
static const int kKindOrder[] = { DK_VIDEO, DK_VIDEOONLY, DK_AUDIO, DK_SUBS, DK_MANUAL };
static const Choice kSubFmts[] = {
    { L"SRT", L"SRT", N_(L"leggibile da qualsiasi lettore (serve FFmpeg)") },
    { N_(L"Originale"), N_(L"Originale"), N_(L"come fornito dal sito (VTT, ASS...)") },
};
static bool KindIsVideo(int k) { return k == DK_VIDEO || k == DK_VIDEOONLY; }
// Qualita' video: altezza massima (0 = la migliore disponibile)
// -1 = "Chiedi ogni volta": analizza il link e propone le risoluzioni disponibili
static const int kVideoHeights[] = { 0, 2160, 1440, 1080, 720, 480, 360, -1 };
static const int kAskQuality = 7;
static const Choice kVideoQ[] = {
    { L"Auto", L"Auto", N_(L"la risoluzione più alta disponibile") },
    { L"2160p (4K)", L"2160p (4K)", nullptr },
    { L"1440p (2K)", L"1440p (2K)", nullptr },
    { L"1080p (Full HD)", L"1080p (Full HD)", nullptr },
    { L"720p (HD)", L"720p (HD)", nullptr },
    { L"480p", L"480p", nullptr },
    { L"360p", L"360p", nullptr },
    { N_(L"Chiedi ogni volta"), N_(L"Chiedi ogni volta"), N_(L"mostra le risoluzioni del video prima di scaricare") },
};
static const Choice kContainers[] = {
    { L"MP4", L"MP4", N_(L"compatibile con tutto: TV, telefoni, editor video") },
    { L"MKV", L"MKV", N_(L"accetta qualsiasi codec, più tracce audio e sottotitoli") },
};
// Formati audio: 0 MP3, 1 M4A, 2 originale
static const Choice kAudioFmts[] = {
    { L"MP3", L"MP3", N_(L"compatibile ovunque") },
    { L"M4A (AAC)", L"M4A", N_(L"qualità migliore a parità di dimensione") },
    { N_(L"Originale"), N_(L"Originale"), N_(L"nessuna conversione: come fornito dal sito") },
};
static const Choice kAudioQ[] = {
    { N_(L"Migliore (VBR)"), N_(L"Migliore"), N_(L"qualità massima, bitrate variabile") },
    { L"320 kbps", L"320 kbps", nullptr },
    { L"192 kbps", L"192 kbps", nullptr },
    { L"128 kbps", L"128 kbps", nullptr },
};
static const wchar_t* kAudioQArg[] = { L"0", L"320K", L"192K", L"128K" };
static const wchar_t* kAudioQFfmpegMp3[] = { L"-q:a 0", L"-b:a 320k", L"-b:a 192k", L"-b:a 128k" };
static const wchar_t* kAudioQFfmpegAac[] = { L"-b:a 256k", L"-b:a 320k", L"-b:a 192k", L"-b:a 128k" };

template <class T, size_t N> constexpr int CountOf(const T (&)[N]) { return (int)N; }

// Preimpostazione di un download (copiata nell'elemento quando entra in coda)
struct Preset {
    int kind = DK_VIDEO;
    int vq = 0;          // indice in kVideoQ
    int container = 0;   // 0 MP4, 1 MKV
    int afmt = 0;        // indice in kAudioFmts
    int aq = 0;          // indice in kAudioQ
    int height = 0;      // altezza scelta al momento (finestra "Scegli la risoluzione"), 0 = da vq
    int subfmt = 0;      // indice in kSubFmts (tipo Solo sottotitoli)
};

struct NativeCtx;

struct Item {
    int id = 0;
    std::wstring url, title, folder, filePath, err;
    std::wstring pct, size, speed, eta, mediaId, thumbUrl;
    Preset preset;
    int container = 0;       // 0 = MP4, 1 = MKV (anche per le scelte manuali)
    bool playlist = false, subs = false;
    // scelta manuale dalla finestra Formati
    bool custom = false;
    int customKind = 0;      // 0 video, 1 solo audio, 2 solo sottotitoli
    bool customSubs = false;
    std::wstring customArgs, customLabel;
    std::vector<std::wstring> parts;   // file parziali del motore nativo (eliminati se si rimuove l'elemento)
    int subsWritten = 0;               // file di sottotitoli scritti (tipo Solo sottotitoli)
    ULONGLONG procSince = 0;           // inizio della fase "Creazione file" (unione/conversione), 0 = non in corso
    // stato
    int plIdx = 0, plCount = 0;
    double pctValue = 0;
    St st = St::Queued;
    bool cancelReq = false;
    bool nativeTried = false;
    bool usingNative = false;
    bool dirty = false;      // ridisegno raggruppato dal timer della UI
    HANDLE hProc = nullptr, hJob = nullptr, hThread = nullptr;
    std::shared_ptr<NativeCtx> native;
    std::unique_ptr<Gdiplus::Bitmap> thumb;
};

struct Settings {
    std::wstring folder, ytdlp, ffmpeg, subLangs = L"it.*,en.*", extra;
    Preset preset;
    int maxDl = 3, conns = 8, theme = 0, lang = 0;   // lang: 0 come Windows, 1 italiano, 2 inglese
    bool playlist = false, subs = false, autoSubs = false, segmented = true, tray = true;
    WINDOWPLACEMENT wp{};
    bool hasWp = false;
};

// Palette "Rubino": neutri tinti di prugna, accento magenta-rubino, gradiente magenta -> rubino come l'icona
struct Palette {
    COLORREF bg, header, surface, surface2, field, border, text, muted, accent, accentHot, accentDown, onAccent, green, red, track;
    COLORREF gradA, gradB;   // estremi del gradiente (pulsante principale, barre, spunte)
};
static const Palette kDark = {
    RGB(19, 12, 19), RGB(27, 17, 27), RGB(35, 22, 35), RGB(52, 33, 51), RGB(42, 27, 42), RGB(72, 48, 71),
    RGB(247, 239, 245), RGB(190, 168, 186), RGB(232, 56, 140), RGB(244, 92, 164), RGB(196, 34, 116), RGB(255, 255, 255),
    RGB(86, 206, 128), RGB(255, 108, 132), RGB(62, 40, 61),
    RGB(206, 58, 226), RGB(226, 30, 82)
};
// Tema chiaro: grigi neutri appena caldi (niente rosa diffuso), card bianche sul fondo grigio,
// accento rubino piu' profondo; il colore resta solo su pulsante principale, barre e icone attive.
static const Palette kLight = {
    RGB(238, 236, 239), RGB(246, 245, 247), RGB(255, 255, 255), RGB(244, 240, 244), RGB(255, 255, 255), RGB(212, 208, 214),
    RGB(28, 24, 30), RGB(104, 98, 108), RGB(170, 28, 96), RGB(190, 40, 112), RGB(140, 20, 78), RGB(255, 255, 255),
    RGB(26, 128, 70), RGB(190, 30, 60), RGB(226, 223, 228),
    RGB(150, 40, 170), RGB(186, 24, 70)
};

// Icone (Segoe Fluent Icons su Windows 11, Segoe MDL2 Assets su Windows 10: stessi codici)
enum : wchar_t {
    G_DOWNLOAD = 0xE896, G_PASTE = 0xE77F, G_FILTER = 0xE71C, G_MORE = 0xE712, G_FOLDER = 0xE8B7,
    G_VIDEO = 0xE714, G_AUDIO = 0xE8D6, G_CC = 0xE7F0, G_LIST = 0xE8FD, G_CANCEL = 0xE711,
    G_REFRESH = 0xE72C, G_OPENFOLDER = 0xED25, G_LINK = 0xE71B, G_CHEVRON = 0xE70D, G_SUN = 0xE706,
    G_MOON = 0xE708, G_AUTO = 0xE790, G_CHECK = 0xE73E, G_ERROR = 0xE783, G_SEARCH = 0xE721,
    G_SETTINGS = 0xE713, G_DELETE = 0xE74D, G_PROFILE = 0xE9E9, G_COPY = 0xE8C8,
};

// ---------------------------------------------------------------------------
// Stato globale
// ---------------------------------------------------------------------------
static HINSTANCE g_inst;
static HWND g_hwnd, g_url, g_list;
static HWND g_bPaste, g_bFormats, g_bDownload, g_bTheme, g_bMore, g_bProfile, g_bSettings, g_bFolder;
static HFONT g_fUi, g_fBold, g_fSmall, g_fSmallBold, g_fTitle, g_fHead, g_fIcon, g_fIconSm, g_fIconMd, g_fIconLg;
static std::wstring g_iconFace = L"Segoe MDL2 Assets";
static UINT g_dpi = 96;
static Palette P = kDark;
static bool g_dark = true;
static HBRUSH g_brBg, g_brHeader, g_brField;
static RECT g_rcHeader, g_rcUrl, g_rcList, g_rcFooter;
static HWND g_hotBtn;
static int g_hotItem = -1;
static int g_hotPart = 0;          // 0 = nessuno, 1 = azione principale, 2 = rimuovi
static bool g_urlFocus = false;
static std::wstring g_footRight;
static std::wstring g_exeDir, g_dataDir, g_ini;
static Settings g_set;
static std::vector<std::unique_ptr<Item>> g_items;
static int g_nextId = 1;
static volatile LONG g_toolBusy = 0;
struct PendingAnalysis {
    std::wstring url;
    bool quick;          // true = finestra "Scegli la risoluzione", false = finestra Formati completa
};
static std::deque<PendingAnalysis> g_analyzeQueue;
static bool g_analyzing = false;
static HICON g_appIcon;
static HANDLE g_connSlots;          // semaforo del budget globale di connessioni
static bool g_trayAdded = false;
static bool g_quitting = false;

// Prototipi usati tra i moduli
static void InvalidateItem(int id);
static void UpdateStatus();
static void RefreshList();
static void Layout();

// ---------------------------------------------------------------------------
// Utilita'
// ---------------------------------------------------------------------------
static int S(int v) { return MulDiv(v, (int)g_dpi, 96); }

static UINT WndDpi(HWND h) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn && h) return fn(h);
    HDC dc = GetDC(nullptr);
    UINT d = (UINT)GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    return d;
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) ++a;
    while (b > a && iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

static bool StartsWith(const std::wstring& s, const wchar_t* p) {
    size_t n = wcslen(p);
    return s.size() >= n && s.compare(0, n, p) == 0;
}

static std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

static std::wstring BytesToW(const std::string& s) {
    if (s.empty()) return {};
    UINT cp = CP_UTF8;
    DWORD fl = MB_ERR_INVALID_CHARS;
    int n = MultiByteToWideChar(cp, fl, s.data(), (int)s.size(), nullptr, 0);
    if (n == 0) { cp = CP_ACP; fl = 0; n = MultiByteToWideChar(cp, fl, s.data(), (int)s.size(), nullptr, 0); }
    std::wstring w(n, L'\0');
    MultiByteToWideChar(cp, fl, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::wstring U(const std::string& s) { return BytesToW(s); }

// UTF-8 tollerante: le sequenze non valide diventano U+FFFD invece di far ripiegare l'intera riga
// sulla codepage ANSI (yt-dlp scrive sempre UTF-8, forzato da PYTHONIOENCODING)
static std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

// Quoting compatibile con CommandLineToArgvW / MSVCRT
static std::wstring Quote(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    for (auto it = a.begin();; ++it) {
        unsigned bs = 0;
        while (it != a.end() && *it == L'\\') { ++it; ++bs; }
        if (it == a.end()) { r.append(bs * 2, L'\\'); break; }
        if (*it == L'"') { r.append(bs * 2 + 1, L'\\'); r += L'"'; }
        else { r.append(bs, L'\\'); r += *it; }
    }
    r += L'"';
    return r;
}

// Per i messaggi che non devono andare persi (fine di un download, di un'analisi, degli strumenti):
// se la coda dei messaggi e' piena PostMessage fallisce, quindi si ritenta per qualche secondo.
static bool PostRetry(UINT msg, WPARAM wp, LPARAM lp) {
    for (int i = 0; i < 400; ++i) {
        if (PostMessageW(g_hwnd, msg, wp, lp)) return true;
        if (!IsWindow(g_hwnd)) return false;
        Sleep(25);
    }
    return false;
}

static void PostStrRetry(UINT msg, WPARAM wp, const std::wstring& s) {
    auto* p = new std::wstring(s);
    if (!PostRetry(msg, wp, (LPARAM)p)) delete p;
}

static void PostStr(UINT msg, WPARAM wp, const std::wstring& s) {
    auto* p = new std::wstring(s);
    if (!PostMessageW(g_hwnd, msg, wp, (LPARAM)p)) delete p;
}

static std::wstring Win32Err(DWORD e) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring s = buf ? Trim(buf) : TR(L"errore ") + std::to_wstring(e);
    if (buf) LocalFree(buf);
    return s;
}

static std::wstring FormatBytes(double b) {
    if (b <= 0) return L"";
    const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    int i = 0;
    while (b >= 1024 && i < 4) { b /= 1024; ++i; }
    wchar_t buf[32];
    swprintf(buf, 32, i == 0 ? L"%.0f %ls" : (b < 10 ? L"%.2f %ls" : (b < 100 ? L"%.1f %ls" : L"%.0f %ls")), b, u[i]);
    return buf;
}

static std::wstring FormatEta(double sec) {
    if (sec <= 0 || sec > 360000) return L"";
    long s = (long)(sec + 0.5);
    wchar_t buf[32];
    if (s >= 3600) swprintf(buf, 32, L"%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else swprintf(buf, 32, L"%02ld:%02ld", s / 60, s % 60);
    return buf;
}

static std::wstring FormatDuration(double sec) {
    if (sec <= 0) return L"";
    return FormatEta(sec);
}

// Nome file valido per Windows
static std::wstring SanitizeName(const std::wstring& s) {
    std::wstring r;
    for (wchar_t c : s) {
        if (c < 32 || wcschr(L"<>:\"/\\|?*", c)) r += L'_';
        else r += c;
    }
    while (!r.empty() && (r.back() == L'.' || r.back() == L' ')) r.pop_back();
    if (r.size() > 180) r.resize(180);
    if (r.empty()) r = L"video";
    return r;
}

static std::wstring UniquePath(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return path;
    size_t dot = path.find_last_of(L'.');
    size_t sl = path.find_last_of(L'\\');
    std::wstring base = (dot != std::wstring::npos && (sl == std::wstring::npos || dot > sl)) ? path.substr(0, dot) : path;
    std::wstring ext = base.size() < path.size() ? path.substr(base.size()) : L"";
    for (int i = 1; i < 1000; ++i) {
        std::wstring p = base + L" (" + std::to_wstring(i) + L")" + ext;
        if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) return p;
    }
    return path;
}

static std::wstring ContainerExt(int c) { return c == 1 ? L"mkv" : L"mp4"; }

// ---------------------------------------------------------------------------
// Percorsi e impostazioni
// ---------------------------------------------------------------------------
static bool DirWritable(const std::wstring& dir) {
    std::wstring t = dir + L".stackly_write_test";
    HANDLE h = CreateFileW(t.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

static void InitPaths() {
    wchar_t buf[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    g_exeDir = buf;
    g_exeDir.erase(g_exeDir.find_last_of(L'\\') + 1);
    // Portabile se la cartella dell'exe e' scrivibile, altrimenti %LOCALAPPDATA%\Stackly
    if (DirWritable(g_exeDir)) {
        g_dataDir = g_exeDir;
    } else {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
            g_dataDir = std::wstring(p) + L"\\Stackly\\";
            CoTaskMemFree(p);
        } else {
            g_dataDir = g_exeDir;
        }
    }
    SHCreateDirectoryExW(nullptr, (g_dataDir + L"bin").c_str(), nullptr);
    g_ini = g_dataDir + L"Stackly.ini";
}

static std::wstring BinDir() { return g_dataDir + L"bin\\"; }

static std::wstring ResolveTool(const std::wstring& configured, const wchar_t* exe) {
    if (!configured.empty() && FileExists(configured)) return configured;
    for (const std::wstring& dir : { BinDir(), g_exeDir + L"bin\\", g_exeDir }) {
        if (FileExists(dir + exe)) return dir + exe;
    }
    wchar_t buf[MAX_PATH * 2];
    if (SearchPathW(nullptr, exe, nullptr, (DWORD)std::size(buf), buf, nullptr)) return buf;
    return {};
}

static std::wstring IniGet(const wchar_t* k, const wchar_t* def = L"") {
    wchar_t b[4096];
    GetPrivateProfileStringW(L"Stackly", k, def, b, (DWORD)std::size(b), g_ini.c_str());
    return b;
}
static int IniInt(const wchar_t* k, int def) {
    return (int)GetPrivateProfileIntW(L"Stackly", k, def, g_ini.c_str());
}
static void IniSet(const wchar_t* k, const std::wstring& v) {
    WritePrivateProfileStringW(L"Stackly", k, (L"\"" + v + L"\"").c_str(), g_ini.c_str());
}
static void IniSetInt(const wchar_t* k, int v) { IniSet(k, std::to_wstring(v)); }

static std::wstring DefaultDownloads() {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p))) { r = p; CoTaskMemFree(p); }
    return r;
}

static void LoadSettings() {
    g_set.folder    = IniGet(L"Folder", DefaultDownloads().c_str());
    g_set.ytdlp     = IniGet(L"YtDlpPath");
    g_set.ffmpeg    = IniGet(L"FfmpegPath");
    g_set.subLangs  = IniGet(L"SubLangs", L"it.*,en.*");
    g_set.extra     = IniGet(L"ExtraArgs");
    g_set.preset.kind      = std::clamp(IniInt(L"Kind", 0), 0, CountOf(kKinds) - 1);
    g_set.preset.vq        = std::clamp(IniInt(L"VideoQuality", 0), 0, CountOf(kVideoQ) - 1);
    g_set.preset.container = std::clamp(IniInt(L"Container", 0), 0, 1);
    g_set.preset.afmt      = std::clamp(IniInt(L"AudioFormat", 0), 0, CountOf(kAudioFmts) - 1);
    g_set.preset.aq        = std::clamp(IniInt(L"AudioQuality", 0), 0, CountOf(kAudioQ) - 1);
    g_set.preset.subfmt    = std::clamp(IniInt(L"SubFormat", 0), 0, CountOf(kSubFmts) - 1);
    g_set.maxDl     = std::clamp(IniInt(L"MaxDownloads", 3), 1, 8);
    g_set.conns     = std::clamp(IniInt(L"Connections", 8), 1, 32);
    g_set.theme     = std::clamp(IniInt(L"Theme", 0), 0, 2);
    g_set.lang      = std::clamp(IniInt(L"Language", 0), 0, 2);
    g_set.playlist  = IniInt(L"Playlist", 0) != 0;
    g_set.subs      = IniInt(L"Subtitles", 0) != 0;
    g_set.autoSubs  = IniInt(L"AutoSubtitles", 0) != 0;
    g_set.segmented = IniInt(L"Segmented", 1) != 0;
    g_set.tray      = IniInt(L"CloseToTray", 1) != 0;
    int l = IniInt(L"WinLeft", INT_MIN), t = IniInt(L"WinTop", INT_MIN);
    int r = IniInt(L"WinRight", INT_MIN), b = IniInt(L"WinBottom", INT_MIN);
    if (l != INT_MIN && t != INT_MIN && r > l && b > t) {
        g_set.wp.length = sizeof(WINDOWPLACEMENT);
        g_set.wp.rcNormalPosition = { l, t, r, b };
        g_set.wp.showCmd = IniInt(L"WinMax", 0) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        g_set.hasWp = true;
    }
}

static void SaveSettings() {
    IniSet(L"Folder", g_set.folder);
    IniSet(L"YtDlpPath", g_set.ytdlp);
    IniSet(L"FfmpegPath", g_set.ffmpeg);
    IniSet(L"SubLangs", g_set.subLangs);
    IniSet(L"ExtraArgs", g_set.extra);
    IniSetInt(L"Kind", g_set.preset.kind);
    IniSetInt(L"VideoQuality", g_set.preset.vq);
    IniSetInt(L"Container", g_set.preset.container);
    IniSetInt(L"AudioFormat", g_set.preset.afmt);
    IniSetInt(L"AudioQuality", g_set.preset.aq);
    IniSetInt(L"SubFormat", g_set.preset.subfmt);
    IniSetInt(L"MaxDownloads", g_set.maxDl);
    IniSetInt(L"Connections", g_set.conns);
    IniSetInt(L"Theme", g_set.theme);
    IniSetInt(L"Language", g_set.lang);
    IniSetInt(L"Playlist", g_set.playlist);
    IniSetInt(L"Subtitles", g_set.subs);
    IniSetInt(L"AutoSubtitles", g_set.autoSubs);
    IniSetInt(L"Segmented", g_set.segmented);
    IniSetInt(L"CloseToTray", g_set.tray);
    if (g_set.hasWp) {
        const RECT& r = g_set.wp.rcNormalPosition;
        IniSetInt(L"WinLeft", r.left);   IniSetInt(L"WinTop", r.top);
        IniSetInt(L"WinRight", r.right); IniSetInt(L"WinBottom", r.bottom);
        IniSetInt(L"WinMax", g_set.wp.showCmd == SW_SHOWMAXIMIZED);
    }
}

// ---------------------------------------------------------------------------
// Elementi della coda
// ---------------------------------------------------------------------------
static Item* FindItem(int id) {
    for (auto& p : g_items) if (p->id == id) return p.get();
    return nullptr;
}

static bool IsActive(const Item& it) {
    return it.st == St::Starting || it.st == St::Downloading || it.st == St::Processing;
}

static bool ItemOnlySubs(const Item& it) {
    return it.custom ? it.customKind == 2 : it.preset.kind == DK_SUBS;
}

static bool ItemIsAudio(const Item& it) {
    if (it.custom) return it.customKind == 1;
    return it.preset.kind == DK_AUDIO;
}
