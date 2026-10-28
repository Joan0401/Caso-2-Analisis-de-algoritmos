// json.hpp - Lector/escritor JSON minimo (sin dependencias externas).
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace std;

namespace tj {

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj };
    Type t = Null;
    bool b = false;
    double n = 0;
    string s;
    vector<Json> a;
    vector<pair<string, Json>> o;

    Json() {}
    Json(bool v) : t(Bool), b(v) {}
    Json(double v) : t(Num), n(v) {}
    template <class T, typename enable_if<is_integral<T>::value && !is_same<T, bool>::value, int>::type = 0>
    Json(T v) : t(Num), n(static_cast<double>(v)) {}
    Json(const char* v) : t(Str), s(v) {}
    Json(const string& v) : t(Str), s(v) {}

    static Json array() { Json j; j.t = Arr; return j; }
    static Json object() { Json j; j.t = Obj; return j; }

    bool isNull() const { return t == Null; }
    bool isObj() const { return t == Obj; }
    bool isArr() const { return t == Arr; }
    bool isNum() const { return t == Num; }
    bool isStr() const { return t == Str; }

    static const Json& nulo() { static const Json j; return j; }

    bool has(const string& k) const {
        if (t != Obj) return false;
        for (auto& p : o) if (p.first == k) return true;
        return false;
    }
    const Json& operator[](const string& k) const {
        if (t == Obj) for (auto& p : o) if (p.first == k) return p.second;
        return nulo();
    }
    const Json& at(size_t i) const { return (t == Arr && i < a.size()) ? a[i] : nulo(); }
    size_t size() const { return t == Arr ? a.size() : (t == Obj ? o.size() : 0); }

    double num(double d = 0) const { return t == Num ? n : (t == Bool ? (b ? 1.0 : 0.0) : d); }
    int integer(int d = 0) const { return t == Num ? static_cast<int>(llround(n)) : d; }
    string str(const string& d = "") const { return t == Str ? s : d; }
    bool boolean(bool d = false) const { return t == Bool ? b : (t == Num ? n != 0 : d); }

    Json& set(const string& k, Json v) {
        if (t != Obj) { t = Obj; o.clear(); }
        for (auto& p : o) if (p.first == k) { p.second = move(v); return p.second; }
        o.emplace_back(k, move(v));
        return o.back().second;
    }
    Json& push(Json v) {
        if (t != Arr) { t = Arr; a.clear(); }
        a.push_back(move(v));
        return a.back();
    }

    string dump(int indent = -1) const {
        string out;
        write(out, indent, 0);
        return out;
    }

    static Json parse(const string& txt) {
        size_t i = 0;
        Json j = parseValue(txt, i);
        skipWs(txt, i);
        if (i != txt.size()) fail(i, "texto sobrante");
        return j;
    }

private:
    [[noreturn]] static void fail(size_t i, const char* m) {
        throw runtime_error(string("JSON invalido en posicion ") + to_string(i) + ": " + m);
    }
    static void skipWs(const string& s, size_t& i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) i++;
    }
    static void putUtf8(string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    static unsigned hex4(const string& s, size_t& i) {
        if (i + 4 > s.size()) fail(i, "escape \\u incompleto");
        unsigned v = 0;
        for (int k = 0; k < 4; k++) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else fail(i, "hex invalido");
        }
        return v;
    }
    static string parseString(const string& s, size_t& i) {
        if (s[i] != '"') fail(i, "se esperaba comilla");
        i++;
        string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = hex4(s, i);
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = hex4(s, i);
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    putUtf8(out, cp);
                    break;
                }
                default: fail(i, "escape desconocido");
            }
        }
        fail(i, "cadena sin cerrar");
    }
    static Json parseValue(const string& s, size_t& i) {
        skipWs(s, i);
        if (i >= s.size()) fail(i, "fin inesperado");
        char c = s[i];
        if (c == '{') {
            Json j = object();
            i++;
            skipWs(s, i);
            if (i < s.size() && s[i] == '}') { i++; return j; }
            while (true) {
                skipWs(s, i);
                string k = parseString(s, i);
                skipWs(s, i);
                if (i >= s.size() || s[i] != ':') fail(i, "se esperaba ':'");
                i++;
                j.o.emplace_back(k, parseValue(s, i));
                skipWs(s, i);
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; return j; }
                fail(i, "se esperaba ',' o '}'");
            }
        }
        if (c == '[') {
            Json j = array();
            i++;
            skipWs(s, i);
            if (i < s.size() && s[i] == ']') { i++; return j; }
            while (true) {
                j.a.push_back(parseValue(s, i));
                skipWs(s, i);
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; return j; }
                fail(i, "se esperaba ',' o ']'");
            }
        }
        if (c == '"') return Json(parseString(s, i));
        if (s.compare(i, 4, "true") == 0) { i += 4; return Json(true); }
        if (s.compare(i, 5, "false") == 0) { i += 5; return Json(false); }
        if (s.compare(i, 4, "null") == 0) { i += 4; return Json(); }
        const char* st = s.c_str() + i;
        char* en = nullptr;
        double v = strtod(st, &en);
        if (en == st) fail(i, "token inesperado");
        i += static_cast<size_t>(en - st);
        return Json(v);
    }
    static void esc(string& out, const string& v) {
        out += '"';
        for (unsigned char c : v) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf; }
                    else out += static_cast<char>(c);
            }
        }
        out += '"';
    }
    void write(string& out, int ind, int lvl) const {
        auto nl = [&](int l) { if (ind >= 0) { out += '\n'; out.append(static_cast<size_t>(l * ind), ' '); } };
        switch (t) {
            case Null: out += "null"; break;
            case Bool: out += b ? "true" : "false"; break;
            case Num: {
                if (!isfinite(n)) { out += "null"; break; }
                char buf[40];
                if (fabs(n) < 1e15 && n == floor(n)) snprintf(buf, sizeof buf, "%.0f", n);
                else snprintf(buf, sizeof buf, "%.10g", n);
                out += buf;
                break;
            }
            case Str: esc(out, s); break;
            case Arr:
                out += '[';
                for (size_t k = 0; k < a.size(); k++) {
                    if (k) out += ',';
                    nl(lvl + 1);
                    a[k].write(out, ind, lvl + 1);
                }
                if (!a.empty()) nl(lvl);
                out += ']';
                break;
            case Obj:
                out += '{';
                for (size_t k = 0; k < o.size(); k++) {
                    if (k) out += ',';
                    nl(lvl + 1);
                    esc(out, o[k].first);
                    out += ind >= 0 ? ": " : ":";
                    o[k].second.write(out, ind, lvl + 1);
                }
                if (!o.empty()) nl(lvl);
                out += '}';
                break;
        }
    }
};

}  // namespace tj
