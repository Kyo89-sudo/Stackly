// Motore di download segmentato nativo (WinHTTP, nessun tool esterno)
// - divide il file in blocchi e li scarica con richieste HTTP Range su N connessioni
// - ripresa: un file ".stkp" accanto al .part tiene traccia dei blocchi completati
// - annullamento immediato chiudendo le richieste attive
#pragma once
#include <windows.h>
#include <winioctl.h>
#include <winhttp.h>
#include <wincodec.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

namespace net {

struct Url {
    std::wstring host, path;
    INTERNET_PORT port = 0;
    bool https = true;
};

inline bool Crack(const std::wstring& url, Url& u) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[512], path[8192], extra[16384];
    uc.lpszHostName = host;  uc.dwHostNameLength = (DWORD)std::size(host);
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = (DWORD)std::size(path);
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = (DWORD)std::size(extra);
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) return false;
    u.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    u.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    u.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (u.path.empty()) u.path = L"/";
    u.port = uc.nPort;
    u.https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}

// h2 = false per il download segmentato: con HTTP/2 WinHTTP multiplexa tutte le richieste verso lo
// stesso host su UNA sola connessione TCP, annullando il vantaggio delle connessioni multiple.
inline HINTERNET OpenSession(const std::wstring& ua, bool h2 = true, int maxConns = 0) {
    const wchar_t* agent = ua.empty() ? L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) Stackly" : ua.c_str();
    HINTERNET s = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return nullptr;
    WinHttpSetTimeouts(s, 15000, 15000, 20000, 30000);
    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(s, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
#ifdef WINHTTP_PROTOCOL_FLAG_HTTP2
    if (h2) {
        DWORD v = WINHTTP_PROTOCOL_FLAG_HTTP2;
        WinHttpSetOption(s, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &v, sizeof(v));
    }
#endif
    if (maxConns > 0) {
        DWORD mc = (DWORD)maxConns;
        WinHttpSetOption(s, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &mc, sizeof(mc));
        WinHttpSetOption(s, WINHTTP_OPTION_MAX_CONNS_PER_1_0_SERVER, &mc, sizeof(mc));
    }
    return s;
}

inline std::wstring ErrText(DWORD e) {
    switch (e) {
    case ERROR_WINHTTP_TIMEOUT: return L"timeout di rete";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return L"server non trovato";
    case ERROR_WINHTTP_CANNOT_CONNECT: return L"connessione rifiutata";
    case ERROR_WINHTTP_CONNECTION_ERROR: return L"connessione interrotta";
    case ERROR_WINHTTP_SECURE_FAILURE: return L"errore TLS/certificato";
    case ERROR_WINHTTP_OPERATION_CANCELLED: return L"annullato";
    default: return L"errore di rete " + std::to_wstring(e);
    }
}

struct Job {
    // ingresso
    std::wstring url, headers, ua, outPath;
    int connections = 8;
    int64_t chunk = 4ll << 20;      // 4 MiB: dimensione gradita anche ai server video
    int64_t size = -1;              // noto dopo Probe()
    bool ranges = false;
    HANDLE slots = nullptr;         // semaforo globale: limite di connessioni tra tutti i download
    // stato condiviso
    std::atomic<bool>* cancel = nullptr;
    std::atomic<int64_t>* done = nullptr;
    std::atomic<int64_t>* resumed = nullptr;   // byte gia' su disco alla ripresa (esclusi da velocita'/ETA)
    std::mutex mu;
    std::vector<HINTERNET> active;

    void track(HINTERNET h) { std::lock_guard<std::mutex> l(mu); active.push_back(h); }
    // Restituisce true se l'handle era ancora tracciato (e quindi va chiuso dal chiamante)
    bool untrack(HINTERNET h) {
        std::lock_guard<std::mutex> l(mu);
        auto it = std::find(active.begin(), active.end(), h);
        if (it == active.end()) return false;
        active.erase(it);
        return true;
    }
    void abortAll() {
        std::lock_guard<std::mutex> l(mu);
        for (HINTERNET h : active) WinHttpCloseHandle(h);
        active.clear();
    }
    bool cancelled() const { return cancel && cancel->load(); }
};

// Apre una richiesta GET (con Range opzionale) e attende la risposta.
inline HINTERNET Request(Job& j, HINTERNET conn, const Url& u, int64_t from, int64_t to, DWORD& status, DWORD& err) {
    status = 0;
    err = 0;
    HINTERNET r = WinHttpOpenRequest(conn, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, u.https ? WINHTTP_FLAG_SECURE : 0);
    if (!r) { err = GetLastError(); return nullptr; }
    j.track(r);
    std::wstring h = j.headers;
    if (from >= 0) {
        h += L"Range: bytes=" + std::to_wstring(from) + L"-" + (to >= 0 ? std::to_wstring(to) : L"") + L"\r\n";
    }
    h += L"Accept-Encoding: identity\r\n";
    BOOL ok = WinHttpSendRequest(r, h.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(r, nullptr);
    if (!ok) {
        err = GetLastError();
        if (j.untrack(r)) WinHttpCloseHandle(r);
        return nullptr;
    }
    DWORD sz = sizeof(status);
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &sz, WINHTTP_NO_HEADER_INDEX);
    return r;
}

inline void Close(Job& j, HINTERNET r) {
    if (r && j.untrack(r)) WinHttpCloseHandle(r);
}

inline std::wstring QueryHeader(HINTERNET r, DWORD what) {
    DWORD len = 0;
    WinHttpQueryHeaders(r, what, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &len, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !len) return {};
    std::wstring s(len / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(r, what, WINHTTP_HEADER_NAME_BY_INDEX, &s[0], &len, WINHTTP_NO_HEADER_INDEX)) return {};
    s.resize(len / sizeof(wchar_t));
    return s;
}

// Scopre dimensione e supporto ai Range con una richiesta "bytes=0-0".
inline bool Probe(Job& j, std::wstring& err) {
    Url u;
    if (!Crack(j.url, u)) { err = L"URL non valido"; return false; }
    HINTERNET s = OpenSession(j.ua);
    if (!s) { err = L"WinHTTP non disponibile"; return false; }
    HINTERNET c = WinHttpConnect(s, u.host.c_str(), u.port, 0);
    bool ok = false;
    if (c) {
        DWORD status, e;
        HINTERNET r = Request(j, c, u, 0, 0, status, e);
        if (!r) {
            err = ErrText(e);
        } else if (status == 206) {
            std::wstring cr = QueryHeader(r, WINHTTP_QUERY_CONTENT_RANGE);  // "bytes 0-0/12345"
            size_t slash = cr.rfind(L'/');
            if (slash != std::wstring::npos && cr.compare(slash + 1, 1, L"*") != 0) {
                j.size = _wtoi64(cr.c_str() + slash + 1);
                j.ranges = j.size > 0;
            }
            ok = true;
        } else if (status == 200) {
            std::wstring cl = QueryHeader(r, WINHTTP_QUERY_CONTENT_LENGTH);
            j.size = cl.empty() ? -1 : _wtoi64(cl.c_str());
            j.ranges = false;
            ok = true;
        } else {
            err = L"HTTP " + std::to_wstring(status);
        }
        Close(j, r);
        WinHttpCloseHandle(c);
    } else {
        err = ErrText(GetLastError());
    }
    WinHttpCloseHandle(s);
    return ok;
}

// Scrittura posizionale su un handle sincrono
inline bool WriteAt(HANDLE f, int64_t off, const void* data, DWORD n) {
    OVERLAPPED ov{};
    ov.Offset = (DWORD)(off & 0xFFFFFFFF);
    ov.OffsetHigh = (DWORD)(off >> 32);
    DWORD w = 0;
    return WriteFile(f, data, n, &w, &ov) && w == n;
}

// Attesa interrompibile dall'annullamento
inline void WaitOrCancel(const Job& j, DWORD ms) {
    for (DWORD t = 0; t < ms && !j.cancelled(); t += 100) Sleep(100);
}

// Scarica tutto il file (segmentato se il server supporta i Range).
inline bool Download(Job& j, std::wstring& err) {
    Url u;
    if (!Crack(j.url, u)) { err = L"URL non valido"; return false; }
    const bool segmented = j.ranges && j.size > 0;
    HINTERNET ses = segmented ? OpenSession(j.ua, false, std::clamp(j.connections, 1, 32) + 2) : OpenSession(j.ua);
    if (!ses) { err = L"WinHTTP non disponibile"; return false; }

    // --- Server senza Range: una sola connessione, scrittura sequenziale ---
    if (!segmented) {
        HANDLE f = CreateFileW(j.outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (f == INVALID_HANDLE_VALUE) { WinHttpCloseHandle(ses); err = L"impossibile creare il file"; return false; }
        bool ok = false;
        HINTERNET c = WinHttpConnect(ses, u.host.c_str(), u.port, 0);
        if (c) {
            DWORD status, e;
            HINTERNET r = Request(j, c, u, -1, -1, status, e);
            if (r && status == 200) {
                std::vector<char> buf(256 * 1024);
                DWORD n = 0;
                ok = true;
                int64_t off = 0;
                while (!j.cancelled()) {
                    if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &n)) { ok = false; err = ErrText(GetLastError()); break; }
                    if (!n) break;
                    if (!WriteAt(f, off, buf.data(), n)) { ok = false; err = L"errore di scrittura su disco"; break; }
                    off += n;
                    if (j.done) *j.done += n;
                }
                if (j.cancelled()) { ok = false; err = L"annullato"; }
                else if (ok && j.size > 0 && off != j.size) { ok = false; err = L"download incompleto (connessione chiusa)"; }
            } else {
                err = r ? L"HTTP " + std::to_wstring(status) : ErrText(e);
            }
            Close(j, r);
            WinHttpCloseHandle(c);
        }
        CloseHandle(f);
        WinHttpCloseHandle(ses);
        return ok;
    }

    // --- Download segmentato ---
    const int64_t size = j.size;
    const int64_t chunk = std::max<int64_t>(j.chunk, 512 * 1024);
    const int nchunks = (int)((size + chunk - 1) / chunk);
    const int conns = std::clamp(j.connections, 1, 32);

    // Mappa di ripresa: [size:int64][chunk:int64][1 byte per blocco]
    std::wstring mapPath = j.outPath + L".stkp";
    std::vector<char> doneMap(nchunks, 0);
    bool resume = false;
    {
        HANDLE m = CreateFileW(mapPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (m != INVALID_HANDLE_VALUE) {
            int64_t hdr[2] = { 0, 0 };
            DWORD rd = 0;
            if (ReadFile(m, hdr, sizeof(hdr), &rd, nullptr) && rd == sizeof(hdr) && hdr[0] == size && hdr[1] == chunk &&
                ReadFile(m, doneMap.data(), (DWORD)nchunks, &rd, nullptr) && rd == (DWORD)nchunks) {
                resume = GetFileAttributesW(j.outPath.c_str()) != INVALID_FILE_ATTRIBUTES;
            }
            CloseHandle(m);
        }
    }
    if (!resume) std::fill(doneMap.begin(), doneMap.end(), 0);

    HANDLE out = CreateFileW(j.outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             resume ? OPEN_EXISTING : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) { WinHttpCloseHandle(ses); err = L"impossibile creare il file"; return false; }
    // File sparse: le scritture fuori ordine dei segmenti non costringono NTFS a riempire
    // di zeri tutto lo spazio precedente (causa tipica di blocchi su file grandi).
    DWORD br = 0;
    if (!resume) DeviceIoControl(out, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &br, nullptr);
    LARGE_INTEGER li;
    li.QuadPart = size;
    SetFilePointerEx(out, li, nullptr, FILE_BEGIN);
    SetEndOfFile(out);
    CloseHandle(out);

    HANDLE map = CreateFileW(mapPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             resume ? OPEN_EXISTING : CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (map != INVALID_HANDLE_VALUE && !resume) {
        int64_t hdr[2] = { size, chunk };
        WriteAt(map, 0, hdr, sizeof(hdr));
        WriteAt(map, sizeof(hdr), doneMap.data(), (DWORD)nchunks);
    }
    std::mutex mapMu;

    if (j.done) {
        int64_t already = 0;
        for (int i = 0; i < nchunks; ++i)
            if (doneMap[i]) already += std::min(chunk, size - (int64_t)i * chunk);
        *j.done += already;
        if (j.resumed) *j.resumed += already;
    }

    std::atomic<int> next{ 0 };
    std::atomic<bool> failed{ false };
    std::mutex errMu;
    std::wstring firstErr;

    auto worker = [&]() {
        HANDLE f = CreateFileW(j.outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        HINTERNET c = WinHttpConnect(ses, u.host.c_str(), u.port, 0);
        std::vector<char> buf(256 * 1024);
        while (f != INVALID_HANDLE_VALUE && c && !failed && !j.cancelled()) {
            int idx = next++;
            if (idx >= nchunks) break;
            if (doneMap[idx]) continue;
            // Prende uno slot di connessione dal budget globale (condiviso tra i download)
            bool gotSlot = false;
            if (j.slots) {
                while (!failed && !j.cancelled()) {
                    if (WaitForSingleObject(j.slots, 200) == WAIT_OBJECT_0) { gotSlot = true; break; }
                }
                if (!gotSlot) break;
            }
            struct SlotGuard { HANDLE h; bool on; ~SlotGuard() { if (on) ReleaseSemaphore(h, 1, nullptr); } } guard{ j.slots, gotSlot };
            const int64_t from = (int64_t)idx * chunk;
            const int64_t to = std::min(size, from + chunk) - 1;
            bool ok = false;
            std::wstring lastErr;
            for (int attempt = 0; attempt < 6 && !ok && !j.cancelled(); ++attempt) {
                if (attempt) WaitOrCancel(j, 500u << std::min(attempt, 4));
                if (j.cancelled()) break;
                int64_t counted = 0;
                DWORD status, e;
                HINTERNET r = Request(j, c, u, from, to, status, e);
                if (!r) { lastErr = ErrText(e); continue; }
                if (status != 206 && !(status == 200 && from == 0 && to == size - 1)) {
                    lastErr = L"HTTP " + std::to_wstring(status);
                    Close(j, r);
                    if (status == 403 || status == 404 || status == 410) break;  // link scaduto: inutile ritentare
                    continue;
                }
                if (status == 206) {   // il server deve restituire esattamente il blocco richiesto
                    std::wstring cr = QueryHeader(r, WINHTTP_QUERY_CONTENT_RANGE);
                    std::wstring want = L"bytes " + std::to_wstring(from) + L"-";
                    if (!cr.empty() && cr.compare(0, want.size(), want) != 0) {
                        lastErr = L"il server ha restituito un intervallo diverso";
                        Close(j, r);
                        continue;
                    }
                }
                int64_t off = from;
                DWORD n = 0;
                bool rdOk = true;
                while (off <= to && !j.cancelled()) {
                    if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &n)) { rdOk = false; lastErr = ErrText(GetLastError()); break; }
                    if (!n) break;
                    DWORD take = (DWORD)std::min<int64_t>(n, to - off + 1);
                    if (!WriteAt(f, off, buf.data(), take)) { rdOk = false; lastErr = L"errore di scrittura su disco"; break; }
                    off += take;
                    counted += take;
                    if (j.done) *j.done += take;
                }
                Close(j, r);
                ok = rdOk && off == to + 1;
                if (!ok) {
                    if (j.done) *j.done -= counted;
                    if (rdOk && lastErr.empty()) lastErr = L"connessione chiusa prima della fine del blocco";
                }
            }
            if (!ok) {
                if (!j.cancelled()) {
                    std::lock_guard<std::mutex> l(errMu);
                    if (firstErr.empty()) firstErr = lastErr;
                }
                failed = true;
                break;
            }
            doneMap[idx] = 1;
            if (map != INVALID_HANDLE_VALUE) {
                std::lock_guard<std::mutex> l(mapMu);
                char one = 1;
                WriteAt(map, 16 + idx, &one, 1);
            }
        }
        if (c) WinHttpCloseHandle(c);
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    };

    std::vector<std::thread> threads;
    int nthreads = std::min(conns, nchunks);
    for (int i = 0; i < nthreads; ++i) {
        try { threads.emplace_back(worker); } catch (...) { break; }   // si prosegue con i thread gia' avviati
    }
    if (threads.empty()) {
        if (map != INVALID_HANDLE_VALUE) CloseHandle(map);
        WinHttpCloseHandle(ses);
        err = L"impossibile avviare i thread di download";
        return false;
    }
    for (auto& t : threads) t.join();

    if (map != INVALID_HANDLE_VALUE) CloseHandle(map);
    WinHttpCloseHandle(ses);

    bool all = std::all_of(doneMap.begin(), doneMap.end(), [](char v) { return v != 0; });
    if (j.cancelled()) { err = L"annullato"; return false; }
    if (!all) { err = firstErr.empty() ? L"download incompleto" : firstErr; return false; }
    DeleteFileW(mapPath.c_str());
    // File completo: toglie l'attributo sparse (nessun buco rimasto)
    HANDLE fin = CreateFileW(j.outPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (fin != INVALID_HANDLE_VALUE) {
        FILE_SET_SPARSE_BUFFER sb{ FALSE };
        DeviceIoControl(fin, FSCTL_SET_SPARSE, &sb, sizeof(sb), nullptr, 0, &br, nullptr);
        CloseHandle(fin);
    }
    return true;
}

// GET semplice in memoria (miniature)
inline bool GetSmall(const std::wstring& url, std::string& body, size_t maxBytes = 8u << 20) {
    Url u;
    if (!Crack(url, u)) return false;
    HINTERNET s = OpenSession(L"");
    if (!s) return false;
    bool ok = false;
    if (HINTERNET c = WinHttpConnect(s, u.host.c_str(), u.port, 0)) {
        if (HINTERNET r = WinHttpOpenRequest(c, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, u.https ? WINHTTP_FLAG_SECURE : 0)) {
            if (WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(r, nullptr)) {
                DWORD status = 0, sz = sizeof(status);
                WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                    &status, &sz, WINHTTP_NO_HEADER_INDEX);
                if (status == 200) {
                    char buf[65536];
                    DWORD n = 0;
                    ok = true;
                    while (WinHttpReadData(r, buf, sizeof(buf), &n) && n) {
                        body.append(buf, n);
                        if (body.size() > maxBytes) { ok = false; break; }
                    }
                }
            }
            WinHttpCloseHandle(r);
        }
        WinHttpCloseHandle(c);
    }
    WinHttpCloseHandle(s);
    return ok && !body.empty();
}

// Decodifica (JPEG/PNG/WebP...) con WIC, ritaglio 16:9 "cover" e ridimensionamento a W x H, BGRA.
inline bool DecodeThumb(const std::string& data, UINT W, UINT H, std::vector<BYTE>& bgra) {
    IWICImagingFactory* fac = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac))))
        return false;
    bool ok = false;
    IWICStream* st = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* fr = nullptr;
    IWICBitmapClipper* clip = nullptr;
    IWICBitmapScaler* sc = nullptr;
    IWICFormatConverter* cv = nullptr;
    do {
        if (FAILED(fac->CreateStream(&st))) break;
        if (FAILED(st->InitializeFromMemory((BYTE*)data.data(), (DWORD)data.size()))) break;
        if (FAILED(fac->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnDemand, &dec))) break;
        if (FAILED(dec->GetFrame(0, &fr))) break;
        UINT w = 0, h = 0;
        fr->GetSize(&w, &h);
        if (!w || !h) break;
        WICRect rc{ 0, 0, (INT)w, (INT)h };
        if ((double)w / h > (double)W / H) { rc.Width = (INT)(h * (double)W / H); rc.X = ((INT)w - rc.Width) / 2; }
        else { rc.Height = (INT)(w * (double)H / W); rc.Y = ((INT)h - rc.Height) / 2; }
        if (FAILED(fac->CreateBitmapClipper(&clip)) || FAILED(clip->Initialize(fr, &rc))) break;
        if (FAILED(fac->CreateBitmapScaler(&sc)) || FAILED(sc->Initialize(clip, W, H, WICBitmapInterpolationModeFant))) break;
        if (FAILED(fac->CreateFormatConverter(&cv)) ||
            FAILED(cv->Initialize(sc, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
            break;
        bgra.resize((size_t)W * H * 4);
        ok = SUCCEEDED(cv->CopyPixels(nullptr, W * 4, (UINT)bgra.size(), bgra.data()));
    } while (false);
    if (cv) cv->Release();
    if (sc) sc->Release();
    if (clip) clip->Release();
    if (fr) fr->Release();
    if (dec) dec->Release();
    if (st) st->Release();
    fac->Release();
    return ok;
}

}  // namespace net
