// Stackly - motore: processi yt-dlp/ffmpeg, download nativo segmentato, strumenti, analisi formati
#pragma once
#include "app.h"
#include <functional>

// ---------------------------------------------------------------------------
// Processi figli
// ---------------------------------------------------------------------------
// Ambiente del processo figlio con output Python forzato in UTF-8.
static std::wstring ChildEnv() {
    std::wstring env;
    LPWCH blk = GetEnvironmentStringsW();
    for (LPWCH p = blk; p && *p; p += wcslen(p) + 1) {
        std::wstring v = p;
        if (_wcsnicmp(v.c_str(), L"PYTHONIOENCODING=", 17) == 0 || _wcsnicmp(v.c_str(), L"PYTHONUTF8=", 11) == 0)
            continue;
        env += v;
        env += L'\0';
    }
    if (blk) FreeEnvironmentStringsW(blk);
    env += L"PYTHONIOENCODING=utf-8";
    env += L'\0';
    env += L"PYTHONUTF8=1";
    env += L'\0';
    env += L'\0';
    return env;
}

struct Spawned {
    HANDLE proc = nullptr;
    HANDLE job = nullptr;
    HANDLE out = nullptr;   // lato lettura dello stdout
};

// Avvia un processo nascosto, a priorita' sotto il normale, dentro un job object
// (chiudere/terminare il job termina anche i processi figli, es. ffmpeg lanciato da yt-dlp).
// stdErr: handle ereditabile per stderr, oppure nullptr per unirlo a stdout.
static bool Spawn(const std::wstring& exe, const std::wstring& args, const std::wstring& cwd, HANDLE stdErr,
                  Spawned& sp, std::wstring& err) {
    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 1 << 16)) { err = Win32Err(GetLastError()); return false; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOEXW six{};
    STARTUPINFOW& si = six.StartupInfo;
    si.cb = sizeof(six);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = stdErr ? stdErr : wr;

    // Il figlio eredita SOLO i propri handle. Senza questa lista erediterebbe ogni handle ereditabile
    // del processo, compresi i tubi degli altri download avviati in parallelo: quei tubi resterebbero
    // aperti nel figlio e il lettore dell'altro download non vedrebbe mai la fine dell'output.
    HANDLE inherit[3];
    DWORD nInherit = 0;
    inherit[nInherit++] = wr;
    if (stdErr && stdErr != wr) inherit[nInherit++] = stdErr;
    if (nul != INVALID_HANDLE_VALUE && nul) inherit[nInherit++] = nul;
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<BYTE> attrBuf(attrSize ? attrSize : 64);
    auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    bool haveList = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, nInherit * sizeof(HANDLE),
                                              nullptr, nullptr);
    if (haveList) six.lpAttributeList = attrs;
    si.cb = haveList ? sizeof(six) : sizeof(STARTUPINFOW);

    std::wstring cmd = Quote(exe) + L" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    std::wstring env = ChildEnv();
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | BELOW_NORMAL_PRIORITY_CLASS |
                                 (haveList ? EXTENDED_STARTUPINFO_PRESENT : 0),
                             (LPVOID)env.c_str(), cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    DWORD le = GetLastError();
    if (haveList) DeleteProcThreadAttributeList(attrs);
    if (!ok && haveList) {   // lista di handle rifiutata (molto raro): avvio classico
        si.cb = sizeof(STARTUPINFOW);
        ok = CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | BELOW_NORMAL_PRIORITY_CLASS,
                            (LPVOID)env.c_str(), cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
        le = GetLastError();
    }
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        err = Win32Err(le);
        return false;
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));
        if (!AssignProcessToJobObject(job, pi.hProcess)) { CloseHandle(job); job = nullptr; }
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    sp.proc = pi.hProcess;
    sp.job = job;
    sp.out = rd;
    return true;
}

// Esegue un processo e ne raccoglie stdout (e stderr separato, se richiesto).
// onJob viene chiamato con il job attivo (per l'annullamento) e poi con nullptr.
static bool RunCaptureEx(const std::wstring& exe, const std::wstring& args, std::string& out, std::string* errOut,
                         DWORD& code, const std::function<void(HANDLE)>& onJob = nullptr) {
    code = 1;
    HANDLE errFile = nullptr;
    if (errOut) {
        wchar_t tmpDir[MAX_PATH], tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpDir);
        GetTempFileNameW(tmpDir, L"stk", 0, tmp);
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        errFile = CreateFileW(tmp, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (errFile == INVALID_HANDLE_VALUE) errFile = nullptr;
    }
    Spawned sp;
    std::wstring e;
    if (!Spawn(exe, args, L"", errFile, sp, e)) {
        if (errOut) *errOut = "ERROR: " + std::string(e.begin(), e.end());
        if (errFile) CloseHandle(errFile);
        return false;
    }
    if (onJob) onJob(sp.job ? sp.job : sp.proc);   // senza job (raro) si annulla terminando il processo
    char buf[65536];
    DWORD n = 0;
    while (ReadFile(sp.out, buf, sizeof(buf), &n, nullptr) && n) out.append(buf, n);
    CloseHandle(sp.out);
    WaitForSingleObject(sp.proc, INFINITE);
    GetExitCodeProcess(sp.proc, &code);
    if (onJob) onJob(nullptr);
    CloseHandle(sp.proc);
    if (sp.job) CloseHandle(sp.job);
    if (errFile) {
        SetFilePointer(errFile, 0, nullptr, FILE_BEGIN);
        while (ReadFile(errFile, buf, sizeof(buf), &n, nullptr) && n) errOut->append(buf, n);
        CloseHandle(errFile);
    }
    return true;
}

// Ultima riga "ERROR: ..." di un output yt-dlp
static std::wstring LastError(const std::string& text) {
    std::wstring w = BytesToW(text), last;
    size_t pos = 0;
    while (pos < w.size()) {
        size_t nl = w.find_first_of(L"\r\n", pos);
        std::wstring line = w.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        if (StartsWith(line, L"ERROR:")) last = Trim(line.substr(6));
        if (nl == std::wstring::npos) break;
        pos = nl + 1;
    }
    return last;
}

// ---------------------------------------------------------------------------
// Argomenti yt-dlp
// ---------------------------------------------------------------------------
static std::wstring CommonArgs(bool withConcurrency) {
    std::wstring c = L"--no-colors --encoding utf-8";
    if (withConcurrency) c += L" -N " + std::to_wstring(std::clamp(g_set.conns, 1, 32));
    std::wstring ff = ResolveTool(g_set.ffmpeg, L"ffmpeg.exe");
    if (!ff.empty()) c += L" --ffmpeg-location " + Quote(ff);
    std::wstring deno = BinDir() + L"deno.exe";
    if (FileExists(deno)) c += L" --js-runtimes " + Quote(L"deno:" + deno);
    if (!Trim(g_set.extra).empty()) c += L" " + Trim(g_set.extra);
    return c;
}

static std::wstring SubLangsArg() {
    return Quote(g_set.subLangs.empty() ? L"it.*,en.*" : g_set.subLangs);
}

// Formato, contenitore e sottotitoli
static std::wstring FormatArgs(const Item& it) {
    if (it.custom) return it.customArgs;
    const Preset& pr = it.preset;
    if (pr.kind == DK_AUDIO) {
        const wchar_t* q = kAudioQArg[std::clamp(pr.aq, 0, CountOf(kAudioQ) - 1)];
        if (pr.afmt == 0) return std::wstring(L"-f \"ba/b\" -x --audio-format mp3 --audio-quality ") + q;
        if (pr.afmt == 1) return std::wstring(L"-f \"ba[ext=m4a]/ba/b\" -x --audio-format m4a --audio-quality ") + q;
        return L"-f \"ba/b\" -x";
    }
    if (pr.kind == DK_SUBS) {
        std::wstring s = L"--skip-download --write-subs --sub-langs " + SubLangsArg();
        if (g_set.autoSubs) s += L" --write-auto-subs";
        if (pr.subfmt == 0 && !ResolveTool(g_set.ffmpeg, L"ffmpeg.exe").empty()) s += L" --convert-subs srt";
        return s;
    }
    const std::wstring cont = ContainerExt(it.container);
    const int h = pr.height > 0 ? pr.height : std::max(0, kVideoHeights[std::clamp(pr.vq, 0, CountOf(kVideoHeights) - 1)]);
    std::wstring sort = h ? L"res:" + std::to_wstring(h) : L"res";
    // A parita' di risoluzione preferisce codec compatibili con MP4 (niente ricodifica, unione piu' veloce)
    if (it.container == 0) sort += L",ext:mp4:m4a";
    if (pr.kind == DK_VIDEOONLY)   // solo la traccia video: niente unione con l'audio
        return L"-f \"bv/bv*\" -S " + Quote(sort) + L" --remux-video " + cont + L" --postprocessor-args " +
               Quote(L"VideoRemuxer:-an");
    std::wstring s = L"-f \"bv*+ba/b\" -S " + Quote(sort) + L" --merge-output-format " + cont + L" --remux-video " + cont;
    if (it.subs) {
        s += L" --embed-subs --sub-langs " + SubLangsArg();
        if (g_set.autoSubs) s += L" --write-auto-subs";
    }
    return s;
}

static bool ItemWantsSubs(const Item& it) {
    if (it.custom) return it.customSubs;
    return (it.subs && it.preset.kind == DK_VIDEO) || it.preset.kind == DK_SUBS;
}

static std::wstring BuildYtdlpArgs(const Item& it) {
    std::wstring c = L"--newline --progress --no-simulate";
    c += L" --progress-template " + Quote(
        L"download:[SK]%(progress._percent_str)s|"
        L"%(progress._total_bytes_str,progress._total_bytes_estimate_str)s|"
        L"%(progress._speed_str)s|%(progress._eta_str)s|"
        L"%(info.playlist_index)s|%(info.playlist_count)s");
    c += L" --print " + Quote(L"video:[SKT]%(title)s");
    c += L" --print " + Quote(L"video:[SKI]%(id)s");
    c += L" --print " + Quote(L"video:[SKH]%(thumbnail)s");
    c += L" --print " + Quote(L"after_move:[SKF]%(filepath)s");
    c += L" " + FormatArgs(it);
    if (it.playlist) {
        c += L" --yes-playlist -o " +
             Quote(L"%(playlist_title,playlist|Playlist)s/%(playlist_index)03d - %(title)s.%(ext)s");
    } else {
        c += L" --no-playlist -o " + Quote(L"%(title)s.%(ext)s");
    }
    c += L" -P " + Quote(it.folder);
    c += L" " + CommonArgs(true);
    c += L" -- " + Quote(it.url);
    return c;
}

// ---------------------------------------------------------------------------
// Miniature (pool di thread di Windows)
// ---------------------------------------------------------------------------
struct ThumbMsg {
    int id;
    std::wstring url;
    std::vector<BYTE> px;
};
static const UINT kThumbW = 320, kThumbH = 180;

static void CALLBACK ThumbWork(PTP_CALLBACK_INSTANCE, void* p) {
    std::unique_ptr<ThumbMsg> m(static_cast<ThumbMsg*>(p));
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string data;
    bool ok = net::GetSmall(m->url, data) && net::DecodeThumb(data, kThumbW, kThumbH, m->px);
    if (!ok && m->url.find(L"/vi_webp/") != std::wstring::npos) {
        // Senza codec WebP: ripiega sulla versione JPEG di YouTube
        std::wstring alt = m->url;
        alt.replace(alt.find(L"/vi_webp/"), 9, L"/vi/");
        size_t w = alt.rfind(L".webp");
        if (w != std::wstring::npos) alt.replace(w, 5, L".jpg");
        data.clear();
        ok = net::GetSmall(alt, data) && net::DecodeThumb(data, kThumbW, kThumbH, m->px);
    }
    if (SUCCEEDED(hr)) CoUninitialize();
    if (ok && PostMessageW(g_hwnd, WM_APP_THUMB, 0, (LPARAM)m.get())) m.release();
}

static void RequestThumb(Item& it, const std::wstring& url) {
    std::wstring u = Trim(url);
    if (u.empty() || u == L"NA" || u == it.thumbUrl) return;
    if (!StartsWith(u, L"http://") && !StartsWith(u, L"https://")) return;
    it.thumbUrl = u;
    it.thumb.reset();
    auto* m = new ThumbMsg{ it.id, u, {} };
    if (!TrySubmitThreadpoolCallback(ThumbWork, m, nullptr)) delete m;
}

static void OnThumb(ThumbMsg* raw) {
    std::unique_ptr<ThumbMsg> m(raw);
    Item* it = FindItem(m->id);
    if (!it || it->thumbUrl != m->url || m->px.size() != (size_t)kThumbW * kThumbH * 4) return;
    auto bmp = std::make_unique<Gdiplus::Bitmap>((INT)kThumbW, (INT)kThumbH, PixelFormat32bppARGB);
    Gdiplus::BitmapData bd{};
    Gdiplus::Rect r(0, 0, (INT)kThumbW, (INT)kThumbH);
    if (bmp->LockBits(&r, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bd) != Gdiplus::Ok) return;
    for (UINT y = 0; y < kThumbH; ++y)
        memcpy((BYTE*)bd.Scan0 + (size_t)y * bd.Stride, m->px.data() + (size_t)y * kThumbW * 4, kThumbW * 4);
    bmp->UnlockBits(&bd);
    it->thumb = std::move(bmp);
    InvalidateItem(it->id);
}

// ---------------------------------------------------------------------------
// Percorso yt-dlp (playlist, sottotitoli, HLS/DASH con frammenti paralleli)
// ---------------------------------------------------------------------------
struct ReaderCtx {
    int id;
    HANDLE rd;
    HANDLE proc;
};

static DWORD WINAPI ReaderProc(LPVOID p) {
    std::unique_ptr<ReaderCtx> c(static_cast<ReaderCtx*>(p));
    std::string acc;
    char buf[8192];
    DWORD n = 0;
    while (ReadFile(c->rd, buf, sizeof(buf), &n, nullptr) && n) {
        for (DWORD i = 0; i < n; ++i) {
            char ch = buf[i];
            if (ch == '\r' || ch == '\n') {
                if (!acc.empty()) PostStr(WM_APP_LINE, c->id, Utf8ToW(acc));
                acc.clear();
            } else if (acc.size() < 64 * 1024) {
                acc += ch;
            }
        }
    }
    if (!acc.empty()) PostStr(WM_APP_LINE, c->id, Utf8ToW(acc));
    WaitForSingleObject(c->proc, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(c->proc, &code);
    PostRetry(WM_APP_DONE, c->id, (LPARAM)code);
    CloseHandle(c->rd);
    CloseHandle(c->proc);
    return 0;
}

static bool StartYtdlp(Item& it) {
    std::wstring ytdlp = ResolveTool(g_set.ytdlp, L"yt-dlp.exe");
    if (ytdlp.empty()) {
        it.st = St::Error;
        it.err = TR(L"yt-dlp.exe non trovato (menu ⋯ > Aggiorna / scarica yt-dlp)");
        return false;
    }
    SHCreateDirectoryExW(nullptr, it.folder.c_str(), nullptr);
    Spawned sp;
    std::wstring e;
    if (!Spawn(ytdlp, BuildYtdlpArgs(it), it.folder, nullptr, sp, e)) {
        it.st = St::Error;
        it.err = TR(L"avvio yt-dlp fallito: ") + e;
        return false;
    }
    HANDLE procDup = nullptr;
    DuplicateHandle(GetCurrentProcess(), sp.proc, GetCurrentProcess(), &procDup, 0, FALSE, DUPLICATE_SAME_ACCESS);
    it.hProc = sp.proc;
    it.hJob = sp.job;
    it.usingNative = false;
    auto* ctx = new ReaderCtx{ it.id, sp.out, procDup };
    it.hThread = CreateThread(nullptr, 64 * 1024, ReaderProc, ctx, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!it.hThread) {
        delete ctx;
        CloseHandle(sp.out);
        CloseHandle(procDup);
        if (sp.job) TerminateJobObject(sp.job, 1); else TerminateProcess(sp.proc, 1);
        it.st = St::Error;
        it.err = TR(L"impossibile creare il thread di lettura");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Percorso nativo: yt-dlp estrae i link, Stackly scarica con N connessioni,
// ffmpeg unisce/converte. Qualsiasi imprevisto -> si passa a yt-dlp.
// ---------------------------------------------------------------------------
struct NativeCtx {
    int id = 0;
    std::wstring url, folder, fmtArgs, commonArgs, ytdlp, ffmpeg;
    int container = 0;
    int conns = 8;
    int audioConv = 0;     // preset audio: 1 mp3, 2 m4a, 3 originale
    int audioQ = 0;        // indice in kAudioQ
    bool custom = false;
    int customKind = 0;
    std::atomic<bool> cancel{ false };
    std::mutex mu;
    HANDLE job = nullptr;
    net::Job* seg = nullptr;

    void Abort() {
        cancel = true;
        std::lock_guard<std::mutex> l(mu);
        if (job && !TerminateJobObject(job, 1)) TerminateProcess(job, 1);
        if (seg) seg->abortAll();
    }
    void SetJob(HANDLE h) {
        std::lock_guard<std::mutex> l(mu);
        job = h;
        if (h && cancel && !TerminateJobObject(h, 1)) TerminateProcess(h, 1);
    }
};

static bool NativeEligible(const Item& it) {
    if (!g_set.segmented || it.nativeTried || it.playlist) return false;
    if (ItemWantsSubs(it)) return false;
    if (it.custom && it.customKind == 2) return false;
    if (!it.custom && it.preset.kind == DK_AUDIO && it.preset.afmt != 2 &&
        ResolveTool(g_set.ffmpeg, L"ffmpeg.exe").empty())
        return false;
    return true;
}

struct NativeStream {
    std::wstring url, headers, ua, ext, formatId, part;
    int64_t size = -1;
    bool ranges = false;
};

static std::wstring StripExt(const std::wstring& p) {
    size_t dot = p.find_last_of(L'.');
    size_t sl = p.find_last_of(L'\\');
    if (dot == std::wstring::npos || (sl != std::wstring::npos && dot < sl)) return p;
    return p.substr(0, dot);
}

static void NativeLine(const NativeCtx& c, const std::wstring& s) { PostStr(WM_APP_LINE, c.id, s); }

static DWORD WINAPI NativeProc(LPVOID p) {
    std::unique_ptr<std::shared_ptr<NativeCtx>> holder(static_cast<std::shared_ptr<NativeCtx>*>(p));
    NativeCtx& c = **holder;
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto finish = [&](UINT msg, LPARAM code) {
        if (SUCCEEDED(coHr)) CoUninitialize();
        PostRetry(msg, c.id, code);
        return 0;
    };
    auto fallback = [&]() { return c.cancel ? finish(WM_APP_DONE, 1) : finish(WM_APP_FALLBACK, 0); };

    // 1) Estrazione: info JSON con i link diretti dei formati scelti
    std::string out, errText;
    DWORD code = 1;
    std::wstring args = L"-J --no-playlist --flat-playlist --no-warnings " + c.fmtArgs + L" -o " + Quote(L"%(title)s.%(ext)s") +
                        L" -P " + Quote(c.folder) + L" " + c.commonArgs + L" -- " + Quote(c.url);
    RunCaptureEx(c.ytdlp, args, out, &errText, code, [&](HANDLE h) { c.SetJob(h); });
    if (c.cancel) return finish(WM_APP_DONE, 1);
    if (code != 0 || out.empty()) return fallback();

    JVal root;
    if (!JParser(out.data(), out.size()).parse(root) || root.t != JVal::Obj) return fallback();
    if (root.str("_type") == "playlist") return fallback();

    std::wstring title = U(root.str("title"));
    if (!title.empty()) NativeLine(c, L"[SKT]" + title);
    if (root.has("id")) NativeLine(c, L"[SKI]" + U(root.str("id")));
    if (root.has("thumbnail")) NativeLine(c, L"[SKH]" + U(root.str("thumbnail")));

    std::vector<const JVal*> fmts;
    if (const JVal* rf = root.get("requested_formats"); rf && rf->t == JVal::Arr && !rf->items.empty()) {
        for (const JVal& f : rf->items) fmts.push_back(&f);
    } else {
        fmts.push_back(&root);
    }

    // Nome finale
    std::wstring target = U(root.str("_filename", root.str("filename")));
    if (target.empty()) target = c.folder + L"\\" + SanitizeName(title) + L"." + U(root.str("ext", "mp4"));
    const bool audioOnly = (!c.custom && c.audioConv) || (c.custom && c.customKind == 1);
    std::wstring finalExt;
    if (!c.custom && c.audioConv == 1) finalExt = L"mp3";
    else if (!c.custom && c.audioConv == 2) finalExt = L"m4a";
    else if (audioOnly && fmts.size() > 1) finalExt = L"mka";
    else if (audioOnly) {
        finalExt = U(fmts[0]->str("ext", "m4a"));
        std::string vc = fmts[0]->str("vcodec", "none");
        if (vc != "none" && !c.ffmpeg.empty())          // il sito offre solo video+audio: si estrae l'audio
            finalExt = fmts[0]->str("acodec").rfind("mp4a", 0) == 0 ? L"m4a" : L"mka";
        else if (finalExt == L"webm" && !c.ffmpeg.empty())
            finalExt = L"opus";                          // come fa yt-dlp -x
    }
    else finalExt = ContainerExt(c.container);
    std::wstring finalPath = UniquePath(StripExt(target) + L"." + finalExt);

    // 2) Flussi: solo HTTP(S) diretti; HLS/DASH restano a yt-dlp (frammenti paralleli con -N)
    std::vector<NativeStream> streams;
    for (const JVal* f : fmts) {
        std::string proto = f->str("protocol");
        std::string url = f->str("url");
        if (url.empty() || (proto != "https" && proto != "http")) return fallback();
        if (!f->str("cookies").empty()) return fallback();
        NativeStream s;
        s.url = U(url);
        s.ext = U(f->str("ext", "bin"));
        s.formatId = SanitizeName(U(f->str("format_id", "f")));
        if (const JVal* hh = f->get("http_headers"); hh && hh->t == JVal::Obj) {
            for (size_t i = 0; i < hh->keys.size(); ++i) {
                if (hh->items[i].t != JVal::Str) continue;
                std::wstring k = U(hh->keys[i]), v = U(hh->items[i].s);
                if (_wcsicmp(k.c_str(), L"User-Agent") == 0) { s.ua = v; continue; }
                if (_wcsicmp(k.c_str(), L"Accept-Encoding") == 0) continue;
                s.headers += k + L": " + v + L"\r\n";
            }
        }
        s.part = StripExt(finalPath) + L".f" + s.formatId + L"." + s.ext + L".part";
        NativeLine(c, L"[SKX]" + s.part);
        streams.push_back(std::move(s));
    }

    const bool needMerge = streams.size() > 1;
    const bool needConv = !c.custom && (c.audioConv == 1 || (c.audioConv == 2 && _wcsicmp(streams[0].ext.c_str(), L"m4a") != 0));
    const bool needRemux = !needConv && streams.size() == 1 && _wcsicmp(streams[0].ext.c_str(), finalExt.c_str()) != 0;
    if ((needMerge || needConv) && c.ffmpeg.empty()) return fallback();

    // 3) Dimensioni e supporto ai Range
    int64_t total = 0;
    bool totalKnown = true;
    for (auto& s : streams) {
        net::Job pj;
        pj.url = s.url; pj.headers = s.headers; pj.ua = s.ua; pj.cancel = &c.cancel;
        std::wstring e;
        { std::lock_guard<std::mutex> l(c.mu); c.seg = &pj; }
        bool probed = net::Probe(pj, e);
        { std::lock_guard<std::mutex> l(c.mu); c.seg = nullptr; }
        if (c.cancel) return finish(WM_APP_DONE, 1);
        if (!probed) return fallback();
        s.size = pj.size;
        s.ranges = pj.ranges;
        if (s.size > 0) total += s.size; else totalKnown = false;
    }
    if (c.cancel) return finish(WM_APP_DONE, 1);

    // 4) Download segmentato, un flusso alla volta (ognuno su N connessioni)
    std::atomic<int64_t> done{ 0 }, resumed{ 0 };
    std::wstring sizeText = totalKnown ? FormatBytes((double)total) : L"";
    for (auto& s : streams) {
        net::Job j;
        j.url = s.url; j.headers = s.headers; j.ua = s.ua; j.outPath = s.part;
        j.size = s.size; j.ranges = s.ranges;
        j.connections = c.conns;
        j.cancel = &c.cancel;
        j.done = &done;
        j.resumed = &resumed;
        j.slots = g_connSlots;
        { std::lock_guard<std::mutex> l(c.mu); c.seg = &j; }
        bool ok = false;
        std::wstring e;
        std::atomic<bool> running{ true };
        std::thread t;
        try {
            t = std::thread([&] { ok = net::Download(j, e); running = false; });
        } catch (...) {   // risorse esaurite: si lascia fare a yt-dlp
            { std::lock_guard<std::mutex> l(c.mu); c.seg = nullptr; }
            return fallback();
        }
        int64_t lastBytes = done.load() - resumed.load();
        ULONGLONG lastTick = GetTickCount64();
        double ema = 0;
        while (running) {
            Sleep(250);
            ULONGLONG now = GetTickCount64();
            if (now - lastTick < 450 && running) continue;
            int64_t b = done.load();
            int64_t fresh = b - resumed.load();   // la velocita' conta solo i byte scaricati ora
            double inst = (fresh - lastBytes) * 1000.0 / (double)std::max<ULONGLONG>(1, now - lastTick);
            lastBytes = fresh;
            ema = ema <= 0 ? inst : ema * 0.7 + inst * 0.3;
            lastTick = now;
            wchar_t pct[32] = L"";
            std::wstring eta;
            if (totalKnown && total > 0) {
                swprintf(pct, 32, L"%.1f%%", 100.0 * (double)b / (double)total);
                if (ema > 1) eta = FormatEta((double)(total - b) / ema);
            }
            NativeLine(c, std::wstring(L"[SK]") + pct + L"|" + (sizeText.empty() ? FormatBytes((double)b) : sizeText) +
                              L"|" + FormatBytes(ema) + L"/s|" + eta + L"||");
        }
        t.join();
        { std::lock_guard<std::mutex> l(c.mu); c.seg = nullptr; }
        if (c.cancel) return finish(WM_APP_DONE, 1);   // i .part restano: "Riprova" riprende da dove era
        if (!ok) {
            // Se si era gia' scaricato qualcosa (almeno ~8 MB in questa sessione) i blocchi completati si
            // conservano: "Riprova" estrae link nuovi e riprende da li'. Altrimenti si passa a yt-dlp.
            if (done.load() - resumed.load() >= (8ll << 20)) {
                NativeLine(c, L"ERROR: " + (e.empty() ? std::wstring(TR(L"download interrotto")) : e) +
                                  TR(L" \u2014 premi Riprova per riprendere da dove era"));
                return finish(WM_APP_DONE, 1);
            }
            for (auto& x : streams) { DeleteFileW(x.part.c_str()); DeleteFileW((x.part + L".stkp").c_str()); }
            return fallback();
        }
    }

    // 5) Unione / conversione / spostamento
    NativeLine(c, L"[SKP]");
    auto runFfmpeg = [&](const std::wstring& a) {
        std::string o;
        DWORD rc = 1;
        RunCaptureEx(c.ffmpeg, L"-hide_banner -loglevel error -nostdin -y " + a, o, nullptr, rc,
                     [&](HANDLE h) { c.SetJob(h); });
        return rc == 0;
    };
    bool ok = true;
    if (needMerge) {
        std::wstring in, map;
        for (size_t i = 0; i < streams.size(); ++i) {
            in += L"-i " + Quote(streams[i].part) + L" ";
            map += L"-map " + std::to_wstring(i) + L" ";
        }
        const std::wstring extra;   // niente -movflags +faststart: riscriverebbe tutto il file una seconda volta
        ok = runFfmpeg(in + map + L"-c copy " + extra + Quote(finalPath));
        if (!ok && !c.cancel && _wcsicmp(finalExt.c_str(), L"mp4") == 0) {
            // Codec non ammessi in MP4: ripiega su MKV invece di fallire
            DeleteFileW(finalPath.c_str());
            finalPath = UniquePath(StripExt(finalPath) + L".mkv");
            ok = runFfmpeg(in + map + L"-c copy " + Quote(finalPath));
        }
    } else if (needConv) {
        int q = std::clamp(c.audioQ, 0, CountOf(kAudioQ) - 1);
        std::wstring codec = c.audioConv == 1 ? std::wstring(L"-c:a libmp3lame ") + kAudioQFfmpegMp3[q]
                                              : std::wstring(L"-c:a aac ") + kAudioQFfmpegAac[q];
        ok = runFfmpeg(L"-i " + Quote(streams[0].part) + L" -vn " + codec + L" " + Quote(finalPath));
    } else if (needRemux && !c.ffmpeg.empty()) {
        const std::wstring extra;   // niente -movflags +faststart: riscriverebbe tutto il file una seconda volta
        std::wstring maps = audioOnly ? L" -vn -c:a copy " : L" -map 0 -c copy ";
        ok = runFfmpeg(L"-i " + Quote(streams[0].part) + maps + extra + Quote(finalPath));
        if (!ok && !c.cancel) {
            DeleteFileW(finalPath.c_str());
            finalPath = UniquePath(StripExt(finalPath) + L"." + streams[0].ext);
            ok = MoveFileExW(streams[0].part.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
        }
    } else {
        if (needRemux) finalPath = UniquePath(StripExt(finalPath) + L"." + streams[0].ext);
        ok = MoveFileExW(streams[0].part.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
    }
    for (auto& s : streams) DeleteFileW(s.part.c_str());
    if (c.cancel) { DeleteFileW(finalPath.c_str()); return finish(WM_APP_DONE, 1); }
    if (!ok) {
        DeleteFileW(finalPath.c_str());
        NativeLine(c, std::wstring(L"ERROR: ") + TR(L"unione/conversione con ffmpeg non riuscita"));
        return finish(WM_APP_DONE, 1);
    }
    NativeLine(c, L"[SKF]" + finalPath);
    return finish(WM_APP_DONE, 0);
}

static bool StartNative(Item& it) {
    auto ctx = std::make_shared<NativeCtx>();
    ctx->id = it.id;
    ctx->url = it.url;
    ctx->folder = it.folder;
    ctx->fmtArgs = FormatArgs(it);
    ctx->commonArgs = CommonArgs(false);
    ctx->conns = std::clamp(g_set.conns, 1, 32);
    ctx->ytdlp = ResolveTool(g_set.ytdlp, L"yt-dlp.exe");
    ctx->ffmpeg = ResolveTool(g_set.ffmpeg, L"ffmpeg.exe");
    ctx->container = it.container;
    ctx->custom = it.custom;
    ctx->customKind = it.customKind;
    ctx->audioConv = (!it.custom && it.preset.kind == DK_AUDIO) ? it.preset.afmt + 1 : 0;
    ctx->audioQ = it.preset.aq;
    if (ctx->ytdlp.empty()) return false;
    SHCreateDirectoryExW(nullptr, it.folder.c_str(), nullptr);
    auto* holder = new std::shared_ptr<NativeCtx>(ctx);
    it.hThread = CreateThread(nullptr, 256 * 1024, NativeProc, holder, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!it.hThread) { delete holder; return false; }
    it.native = ctx;
    it.usingNative = true;
    it.nativeTried = true;
    return true;
}

// ---------------------------------------------------------------------------
// Coda
// ---------------------------------------------------------------------------
static void CloseItemHandles(Item& it) {
    if (it.hThread) { CloseHandle(it.hThread); it.hThread = nullptr; }
    if (it.hProc)   { CloseHandle(it.hProc);   it.hProc = nullptr; }
    if (it.hJob)    { CloseHandle(it.hJob);    it.hJob = nullptr; }
    it.native.reset();
}

static bool StartItem(Item& it) {
    it.err.clear();
    it.cancelReq = false;
    it.st = St::Starting;
    if (ResolveTool(g_set.ytdlp, L"yt-dlp.exe").empty()) {
        it.st = St::Error;
        it.err = TR(L"yt-dlp.exe non trovato (menu ⋯ > Aggiorna / scarica yt-dlp)");
        return false;
    }
    if (NativeEligible(it) && StartNative(it)) return true;
    if (StartYtdlp(it)) { it.st = St::Starting; return true; }
    return false;
}

static void PumpQueue() {
    int active = 0;
    for (auto& p : g_items) if (IsActive(*p)) ++active;
    for (auto& p : g_items) {
        if (active >= g_set.maxDl) break;
        if (p->st == St::Queued) {
            if (StartItem(*p)) ++active;
            InvalidateItem(p->id);
        }
    }
    UpdateStatus();
}

static void CancelItem(Item& it) {
    if (it.st == St::Queued) {
        it.st = St::Cancelled;
    } else if (IsActive(it)) {
        it.cancelReq = true;
        if (it.native) it.native->Abort();
        if (it.hJob) TerminateJobObject(it.hJob, 1);
        else if (it.hProc) TerminateProcess(it.hProc, 1);
    }
    InvalidateItem(it.id);
}

static std::wstring CleanField(const std::wstring& s) {
    std::wstring t = Trim(s);
    if (t == L"NA" || t == L"None" || t.find(L"Unknown") != std::wstring::npos || t.find(L"N/A") != std::wstring::npos)
        return L"";
    return t;
}

static void OnLine(int id, const std::wstring& line) {
    Item* it = FindItem(id);
    if (!it) return;
    if (StartsWith(line, L"[SK]")) {
        std::vector<std::wstring> parts;
        size_t start = 4;
        for (;;) {
            size_t bar = line.find(L'|', start);
            parts.push_back(line.substr(start, bar == std::wstring::npos ? std::wstring::npos : bar - start));
            if (bar == std::wstring::npos) break;
            start = bar + 1;
        }
        parts.resize(6);
        it->pct   = CleanField(parts[0]);
        it->size  = CleanField(parts[1]);
        it->speed = CleanField(parts[2]);
        it->eta   = CleanField(parts[3]);
        int pi = _wtoi(CleanField(parts[4]).c_str()), pc = _wtoi(CleanField(parts[5]).c_str());
        if (pc > 0) { it->plIdx = pi; it->plCount = pc; }
        it->pctValue = std::clamp(_wtof(it->pct.c_str()), 0.0, 100.0);
        bool full = it->pctValue >= 99.95;
        it->st = (full && !it->usingNative) ? St::Processing : St::Downloading;
        if (it->st == St::Processing) {
            it->speed.clear(); it->eta.clear();
            if (!it->procSince) it->procSince = GetTickCount64();
        } else {
            it->procSince = 0;
        }
    } else if (StartsWith(line, L"[SKT]")) {
        it->title = Trim(line.substr(5));
        if (it->st == St::Starting) it->st = St::Downloading;
    } else if (StartsWith(line, L"[SKI]")) {
        std::wstring mid = CleanField(line.substr(5));
        if (mid != it->mediaId) { it->mediaId = mid; it->pctValue = 0; it->pct.clear(); }
    } else if (StartsWith(line, L"[SKH]")) {
        RequestThumb(*it, line.substr(5));
    } else if (StartsWith(line, L"[SKX]")) {
        std::wstring p = Trim(line.substr(5));
        if (std::find(it->parts.begin(), it->parts.end(), p) == it->parts.end()) it->parts.push_back(p);
        return;
    } else if (StartsWith(line, L"[SKF]")) {
        it->filePath = Trim(line.substr(5));
    } else if (StartsWith(line, L"[SKP]")) {
        it->st = St::Processing;
        if (!it->procSince) it->procSince = GetTickCount64();
        it->pctValue = 100;
        it->speed.clear();
        it->eta.clear();
    } else if (line.find(L"Writing video subtitles to: ") != std::wstring::npos) {
        // con --skip-download yt-dlp non stampa after_move: il file dei sottotitoli si ricava da qui
        it->filePath = Trim(line.substr(line.find(L"Writing video subtitles to: ") + 28));
        ++it->subsWritten;
        return;
    } else if (StartsWith(line, L"ERROR:")) {
        it->err = Trim(line.substr(6));
    } else {
        return;
    }
    it->dirty = true;   // ridisegnato dal timer: niente ridisegno a ogni riga
}

static void ShowTrayBalloon(const std::wstring& title, const std::wstring& text);

static void OnDone(int id, DWORD code) {
    Item* it = FindItem(id);
    if (!it) return;
    CloseItemHandles(*it);
    if (it->cancelReq) {
        it->st = St::Cancelled;
    } else if (code == 0 && ItemOnlySubs(*it) && it->subsWritten == 0) {
        it->st = St::Error;
        it->err = TR(L"nessun sottotitolo disponibile nelle lingue scelte (") + g_set.subLangs + L")";
    } else if (code == 0) {
        it->st = St::Done;
        if (ItemOnlySubs(*it) && !it->filePath.empty() && !FileExists(it->filePath)) {
            std::wstring srt = StripExt(it->filePath) + L".srt";   // convertito in SRT dopo la scrittura
            if (FileExists(srt)) it->filePath = srt;
        }
        it->pct = L"100%";
        it->pctValue = 100;
        if (!IsWindowVisible(g_hwnd) || IsIconic(g_hwnd))
            ShowTrayBalloon(TR(L"Download completato"), it->title.empty() ? it->url : it->title);
    } else {
        it->st = St::Error;
        if (it->err.empty()) it->err = TR(L"yt-dlp terminato con codice ") + std::to_wstring((int)code);
    }
    it->speed.clear();
    it->eta.clear();
    InvalidateItem(it->id);
    PumpQueue();
}

static void OnFallback(int id) {
    Item* it = FindItem(id);
    if (!it) return;
    CloseItemHandles(*it);
    if (it->cancelReq) {
        it->st = St::Cancelled;
    } else {
        it->pct.clear();
        it->pctValue = 0;
        it->speed.clear();
        it->eta.clear();
        if (!StartYtdlp(*it)) it->st = St::Error;
    }
    InvalidateItem(it->id);
    PumpQueue();
}

// ---------------------------------------------------------------------------
// Strumenti: yt-dlp, ffmpeg, deno
// ---------------------------------------------------------------------------
enum ToolTask { TT_YTDLP = 1, TT_FFMPEG = 2, TT_DENO = 4 };

static const wchar_t* kUrlYtdlp  = L"https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";
static const wchar_t* kUrlFfmpeg = L"https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip";
static const wchar_t* kUrlDeno   = L"https://github.com/denoland/deno/releases/latest/download/deno-x86_64-pc-windows-msvc.zip";

// Scarica un file con il motore nativo (segmentato se il server lo consente, senza cache)
static bool DownloadTo(const wchar_t* url, const std::wstring& dest, std::wstring& err) {
    DeleteFileW(dest.c_str());
    std::atomic<bool> cancel{ false };
    net::Job j;
    j.url = url;
    j.outPath = dest;
    j.cancel = &cancel;
    j.connections = 8;
    if (!net::Probe(j, err) || !net::Download(j, err) || !FileExists(dest)) {
        err = TR(L"download fallito (") + err + L"): " + url;
        DeleteFileW(dest.c_str());
        return false;
    }
    return true;
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

static std::wstring FindFileRec(const std::wstring& dir, const wchar_t* name) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        std::wstring p = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) found = FindFileRec(p, name);
        else if (!_wcsicmp(fd.cFileName, name)) found = p;
    } while (found.empty() && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

// Scarica uno zip ed estrae i file indicati in bin\ (tar.exe di Windows 10+)
static bool FetchZipTools(const wchar_t* url, const wchar_t* tag, std::initializer_list<const wchar_t*> files,
                          std::wstring& msg) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring work = std::wstring(tmp) + L"Stackly_" + tag;
    DeleteTree(work);
    SHCreateDirectoryExW(nullptr, work.c_str(), nullptr);
    std::wstring zip = work + L"\\pkg.zip";
    std::wstring err;
    if (!DownloadTo(url, zip, err)) { msg += err + L"\n"; return false; }
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring tar = std::wstring(sys) + L"\\tar.exe";
    if (!FileExists(tar)) {
        msg += TR(L"tar.exe non trovato (serve Windows 10 1803 o successivo). Archivio scaricato in:\n") + zip + L"\n";
        return false;
    }
    std::string out;
    DWORD rc = 1;
    RunCaptureEx(tar, L"-xf " + Quote(zip) + L" -C " + Quote(work), out, nullptr, rc);
    bool ok = true;
    for (const wchar_t* f : files) {
        std::wstring src = FindFileRec(work, f);
        if (src.empty()) { msg += std::wstring(f) + TR(L" non trovato nell'archivio.\n"); ok = false; continue; }
        std::wstring dst = BinDir() + f;
        if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
            msg += std::wstring(TR(L"Copia di ")) + f + TR(L" fallita: ") + Win32Err(GetLastError()) + L"\n";
            ok = false;
        }
    }
    DeleteTree(work);
    return ok;
}

struct ToolArgs {
    int tasks;
    std::wstring ytdlp;
};

static DWORD WINAPI ToolProc(LPVOID p) {
    std::unique_ptr<ToolArgs> ta(static_cast<ToolArgs*>(p));
    int tasks = ta->tasks;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::wstring msg;
    if (tasks & TT_YTDLP) {
        const std::wstring& cur = ta->ytdlp;
        if (!cur.empty()) {
            PostStr(WM_APP_TOOLSTATUS, 0, TR(L"Aggiornamento di yt-dlp..."));
            std::string out;
            DWORD rc = 1;
            if (RunCaptureEx(cur, L"-U", out, nullptr, rc)) {
                std::wstring o = Trim(BytesToW(out));
                if (o.size() > 1500) o = L"..." + o.substr(o.size() - 1500);
                msg += L"yt-dlp:\n" + o + L"\n\n";
            } else {
                msg += TR(L"Impossibile eseguire ") + cur + L"\n\n";
            }
        } else {
            PostStr(WM_APP_TOOLSTATUS, 0, TR(L"Download di yt-dlp..."));
            std::wstring dst = BinDir() + L"yt-dlp.exe", part = dst + L".part", err;
            if (DownloadTo(kUrlYtdlp, part, err) && MoveFileExW(part.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING))
                msg += TR(L"yt-dlp installato.\n\n");
            else
                msg += L"yt-dlp: " + (err.empty() ? Win32Err(GetLastError()) : err) + L"\n\n";
        }
    }
    if (tasks & TT_FFMPEG) {
        PostStr(WM_APP_TOOLSTATUS, 0, TR(L"Download di ffmpeg (circa 100 MB)..."));
        if (FetchZipTools(kUrlFfmpeg, L"ffmpeg", { L"ffmpeg.exe", L"ffprobe.exe" }, msg)) msg += TR(L"ffmpeg installato.\n\n");
    }
    if (tasks & TT_DENO) {
        PostStr(WM_APP_TOOLSTATUS, 0, TR(L"Download di Deno..."));
        if (FetchZipTools(kUrlDeno, L"deno", { L"deno.exe" }, msg)) msg += TR(L"Deno installato.\n\n");
    }
    if (SUCCEEDED(hr)) CoUninitialize();
    PostStrRetry(WM_APP_TOOLDONE, 0, Trim(msg));
    return 0;
}

static void RunTool(int tasks) {
    for (auto& p : g_items) {
        if (IsActive(*p)) {
            MessageBoxW(g_hwnd, TR(L"Attendi la fine dei download in corso (o annullali) prima di aggiornare gli strumenti."),
                        kAppName, MB_ICONINFORMATION);
            return;
        }
    }
    if (InterlockedCompareExchange(&g_toolBusy, 1, 0) != 0) {
        MessageBoxW(g_hwnd, TR(L"Un aggiornamento è già in corso."), kAppName, MB_ICONINFORMATION);
        return;
    }
    auto* ta = new ToolArgs{ tasks, ResolveTool(g_set.ytdlp, L"yt-dlp.exe") };
    HANDLE h = CreateThread(nullptr, 0, ToolProc, ta, 0, nullptr);
    if (h) CloseHandle(h);
    else { delete ta; InterlockedExchange(&g_toolBusy, 0); }
}

// ---------------------------------------------------------------------------
// Analisi di un link (finestra Formati)
// ---------------------------------------------------------------------------
struct AnalyzeResult {
    std::wstring url, ytdlp, common;
    bool quick = false;     // true = solo scelta della risoluzione
    std::string json;
    std::wstring err;
};

static void CALLBACK AnalyzeWork(PTP_CALLBACK_INSTANCE, void* p) {
    std::unique_ptr<AnalyzeResult> r(static_cast<AnalyzeResult*>(p));
    if (r->ytdlp.empty()) {
        r->err = TR(L"yt-dlp.exe non trovato (menu \u22ef > Aggiorna / scarica yt-dlp)");
    } else {
        std::string errText;
        DWORD code = 1;
        RunCaptureEx(r->ytdlp, L"-J --no-playlist --flat-playlist --no-warnings " + r->common + L" -- " + Quote(r->url),
                     r->json, &errText, code);
        if (code != 0 || r->json.empty()) {
            r->err = LastError(errText);
            if (r->err.empty()) r->err = TR(L"analisi non riuscita (codice ") + std::to_wstring((int)code) + L")";
            r->json.clear();
        }
    }
    if (PostRetry(WM_APP_ANALYZED, 0, (LPARAM)r.get())) r.release();
}

static void StartNextAnalysis() {
    if (g_analyzing || g_analyzeQueue.empty()) return;
    auto* r = new AnalyzeResult{ g_analyzeQueue.front().url, ResolveTool(g_set.ytdlp, L"yt-dlp.exe"), CommonArgs(false),
                                 g_analyzeQueue.front().quick, {}, {} };
    g_analyzeQueue.pop_front();
    if (!TrySubmitThreadpoolCallback(AnalyzeWork, r, nullptr)) { delete r; return; }
    g_analyzing = true;
    g_footRight = TR(L"Analisi dei formati disponibili...");
    EnableWindow(g_bFormats, FALSE);
    UpdateStatus();
}
