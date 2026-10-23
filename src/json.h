// Parser JSON minimale (solo lettura) per l'output di "yt-dlp -J".
#pragma once
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>

struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<JVal> items;            // Arr: elementi; Obj: valori
    std::vector<std::string> keys;      // Obj: chiavi (parallele a items)

    const JVal* get(const char* k) const {
        if (t != Obj) return nullptr;
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == k) return &items[i];
        return nullptr;
    }
    std::string str(const char* k, const std::string& def = "") const {
        const JVal* v = get(k);
        return (v && v->t == Str) ? v->s : def;
    }
    double num(const char* k, double def = 0) const {
        const JVal* v = get(k);
        return (v && v->t == Num) ? v->n : def;
    }
    bool has(const char* k) const {
        const JVal* v = get(k);
        return v && v->t != Null;
    }
};

class JParser {
public:
    JParser(const char* data, size_t len) : p_(data), e_(data + len) {}

    bool parse(JVal& out) {
        ws();
        if (!value(out, 0)) return false;
        ws();
        return true;
    }

private:
    const char* p_;
    const char* e_;

    void ws() {
        while (p_ < e_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
    }
    bool lit(const char* w) {
        size_t n = strlen(w);
        if ((size_t)(e_ - p_) < n || memcmp(p_, w, n) != 0) return false;
        p_ += n;
        return true;
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += (char)cp;
        else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
        } else {
            o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F));
            o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (e_ - p_ < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = *p_++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& o) {
        if (p_ >= e_ || *p_ != '"') return false;
        ++p_;
        while (p_ < e_) {
            char c = *p_++;
            if (c == '"') return true;
            if (c != '\\') { o += c; continue; }
            if (p_ >= e_) return false;
            char x = *p_++;
            switch (x) {
            case '"': o += '"'; break;
            case '\\': o += '\\'; break;
            case '/': o += '/'; break;
            case 'b': o += '\b'; break;
            case 'f': o += '\f'; break;
            case 'n': o += '\n'; break;
            case 'r': o += '\r'; break;
            case 't': o += '\t'; break;
            case 'u': {
                unsigned cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && e_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {
                    p_ += 2;
                    unsigned lo;
                    if (!hex4(lo)) return false;
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else {   // alta senza bassa: segnaposto, poi il carattere successivo
                        utf8(o, 0xFFFD);
                        cp = (lo >= 0xD800 && lo <= 0xDFFF) ? 0xFFFD : lo;
                    }
                } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                    cp = 0xFFFD;   // surrogato isolato: produrrebbe UTF-8 non valido
                }
                utf8(o, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }
    bool value(JVal& v, int depth) {
        if (depth > 200 || p_ >= e_) return false;
        char c = *p_;
        if (c == '{') {
            v.t = JVal::Obj;
            ++p_; ws();
            if (p_ < e_ && *p_ == '}') { ++p_; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (p_ >= e_ || *p_ != ':') return false;
                ++p_; ws();
                v.keys.push_back(std::move(k));
                v.items.emplace_back();
                if (!value(v.items.back(), depth + 1)) return false;
                ws();
                if (p_ < e_ && *p_ == ',') { ++p_; continue; }
                if (p_ < e_ && *p_ == '}') { ++p_; return true; }
                return false;
            }
        }
        if (c == '[') {
            v.t = JVal::Arr;
            ++p_; ws();
            if (p_ < e_ && *p_ == ']') { ++p_; return true; }
            for (;;) {
                ws();
                v.items.emplace_back();
                if (!value(v.items.back(), depth + 1)) return false;
                ws();
                if (p_ < e_ && *p_ == ',') { ++p_; continue; }
                if (p_ < e_ && *p_ == ']') { ++p_; return true; }
                return false;
            }
        }
        if (c == '"') { v.t = JVal::Str; return string(v.s); }
        if (lit("true")) { v.t = JVal::Bool; v.b = true; return true; }
        if (lit("false")) { v.t = JVal::Bool; v.b = false; return true; }
        if (lit("null")) { v.t = JVal::Null; return true; }
        if (lit("NaN") || lit("Infinity") || lit("-Infinity")) { v.t = JVal::Null; return true; }
        // numero
        const char* start = p_;
        while (p_ < e_ && *p_ && strchr("+-0123456789.eE", *p_) != nullptr) ++p_;
        if (p_ == start) return false;
        v.t = JVal::Num;
        v.n = strtod(std::string(start, p_).c_str(), nullptr);
        return true;
    }
};
