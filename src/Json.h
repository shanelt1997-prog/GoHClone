// Minimal JSON reader (objects, arrays, strings, numbers, bools, null).
// Extension: '//' line comments are accepted so data files can be annotated.
#pragma once
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace json {

struct Value {
    enum class T { Null, Bool, Num, Str, Arr, Obj };
    T t = T::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> a;          // array items
    std::vector<std::string> keys; // object keys (parallel to vals)
    std::vector<Value> vals;       // object values

    bool isObj() const { return t == T::Obj; }
    bool isArr() const { return t == T::Arr; }
    bool isStr() const { return t == T::Str; }
    bool isNum() const { return t == T::Num; }

    const Value* find(const std::string& k) const {
        if (t != T::Obj) return nullptr;
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == k) return &vals[i];
        return nullptr;
    }
    bool has(const std::string& k) const { return find(k) != nullptr; }
    double num(const std::string& k, double def) const {
        const Value* v = find(k);
        if (!v) return def;
        if (v->t == T::Num) return v->n;
        if (v->t == T::Bool) return v->b ? 1.0 : 0.0;
        return def;
    }
    int integer(const std::string& k, int def) const { return (int)num(k, (double)def); }
    bool boolean(const std::string& k, bool def) const {
        const Value* v = find(k);
        if (!v) return def;
        if (v->t == T::Bool) return v->b;
        if (v->t == T::Num) return v->n != 0;
        return def;
    }
    std::string str(const std::string& k, const std::string& def = "") const {
        const Value* v = find(k);
        return (v && v->t == T::Str) ? v->s : def;
    }
    const std::vector<Value>& arr(const std::string& k) const {
        static const std::vector<Value> empty;
        const Value* v = find(k);
        return (v && v->t == T::Arr) ? v->a : empty;
    }
    std::vector<std::string> strArray(const std::string& k) const {
        std::vector<std::string> out;
        for (const Value& v : arr(k))
            if (v.t == T::Str) out.push_back(v.s);
        return out;
    }
};

class Parser {
public:
    explicit Parser(const std::string& text) : p_(text.c_str()), begin_(text.c_str()) {}
    Value parseAll() {
        Value v = parseValue();
        ws();
        if (*p_) fail("trailing characters");
        return v;
    }

private:
    const char* p_;
    const char* begin_;

    [[noreturn]] void fail(const std::string& msg) {
        int line = 1;
        for (const char* c = begin_; c < p_; ++c)
            if (*c == '\n') ++line;
        throw std::runtime_error("JSON error (line " + std::to_string(line) + "): " + msg);
    }
    void ws() {
        for (;;) {
            while (*p_ == ' ' || *p_ == '\t' || *p_ == '\r' || *p_ == '\n') ++p_;
            if (p_[0] == '/' && p_[1] == '/') {
                while (*p_ && *p_ != '\n') ++p_;
                continue;
            }
            break;
        }
    }
    bool lit(const char* w) {
        const char* q = p_;
        while (*w) {
            if (*q != *w) return false;
            ++q;
            ++w;
        }
        p_ = q;
        return true;
    }
    std::string parseString() {
        if (*p_ != '"') fail("expected string");
        ++p_;
        std::string out;
        while (*p_ && *p_ != '"') {
            if (*p_ == '\\') {
                ++p_;
                switch (*p_) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case '/': out += '/'; break;
                case '\\': out += '\\'; break;
                case '"': out += '"'; break;
                case 'u': {
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        ++p_;
                        char c = *p_;
                        code <<= 4;
                        if (c >= '0' && c <= '9') code |= (unsigned)(c - '0');
                        else if (c >= 'a' && c <= 'f') code |= (unsigned)(c - 'a' + 10);
                        else if (c >= 'A' && c <= 'F') code |= (unsigned)(c - 'A' + 10);
                        else fail("bad \\u escape");
                    }
                    if (code < 0x80) out += (char)code;
                    else if (code < 0x800) {
                        out += (char)(0xC0 | (code >> 6));
                        out += (char)(0x80 | (code & 0x3F));
                    } else {
                        out += (char)(0xE0 | (code >> 12));
                        out += (char)(0x80 | ((code >> 6) & 0x3F));
                        out += (char)(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: fail("bad escape");
                }
                ++p_;
            } else {
                out += *p_++;
            }
        }
        if (*p_ != '"') fail("unterminated string");
        ++p_;
        return out;
    }
    Value parseValue() {
        ws();
        Value v;
        if (*p_ == '{') {
            ++p_;
            v.t = Value::T::Obj;
            ws();
            if (*p_ == '}') { ++p_; return v; }
            for (;;) {
                ws();
                std::string k = parseString();
                ws();
                if (*p_ != ':') fail("expected ':'");
                ++p_;
                Value child = parseValue();
                v.keys.push_back(k);
                v.vals.push_back(std::move(child));
                ws();
                if (*p_ == ',') { ++p_; continue; }
                if (*p_ == '}') { ++p_; break; }
                fail("expected ',' or '}'");
            }
        } else if (*p_ == '[') {
            ++p_;
            v.t = Value::T::Arr;
            ws();
            if (*p_ == ']') { ++p_; return v; }
            for (;;) {
                v.a.push_back(parseValue());
                ws();
                if (*p_ == ',') { ++p_; continue; }
                if (*p_ == ']') { ++p_; break; }
                fail("expected ',' or ']'");
            }
        } else if (*p_ == '"') {
            v.t = Value::T::Str;
            v.s = parseString();
        } else if (lit("true")) {
            v.t = Value::T::Bool;
            v.b = true;
        } else if (lit("false")) {
            v.t = Value::T::Bool;
            v.b = false;
        } else if (lit("null")) {
            v.t = Value::T::Null;
        } else {
            char* end = nullptr;
            double d = std::strtod(p_, &end);
            if (end == p_) fail("unexpected character");
            p_ = end;
            v.t = Value::T::Num;
            v.n = d;
        }
        return v;
    }
};

inline Value parse(const std::string& text) { return Parser(text).parseAll(); }

inline Value parseFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open file: " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        text.erase(0, 3); // strip UTF-8 BOM
    try {
        return parse(text);
    } catch (const std::exception& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
}

} // namespace json
