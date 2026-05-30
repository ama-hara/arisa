#pragma once

// ═══════════════════════════════════════════════════════
// Minimal JSON for aria2 RPC
// Recursive descent parser + compact serializer
// No external dependencies
// ═══════════════════════════════════════════════════════

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <charconv>
#include <sstream>

namespace arisa::json {

struct Value;

using Null    = std::nullptr_t;
using Bool    = bool;
using Int     = std::int64_t;
using Double  = double;
using String  = std::string;
using Array   = std::vector<Value>;
using Object  = std::map<std::string, Value>;

struct Value : std::variant<Null, Bool, Int, Double, String, Array, Object> {
    using variant::variant;

    bool is_null()   const { return std::holds_alternative<Null>(*this); }
    bool is_bool()   const { return std::holds_alternative<Bool>(*this); }
    bool is_int()    const { return std::holds_alternative<Int>(*this); }
    bool is_double() const { return std::holds_alternative<Double>(*this); }
    bool is_string() const { return std::holds_alternative<String>(*this); }
    bool is_array()  const { return std::holds_alternative<Array>(*this); }
    bool is_object() const { return std::holds_alternative<Object>(*this); }

    auto as_int()    const -> Int    { return std::get<Int>(*this); }
    auto as_bool()   const -> Bool   { return std::get<Bool>(*this); }
    auto as_double() const -> Double { return std::get<Double>(*this); }
    auto as_string() const -> const String& { return std::get<String>(*this); }
    auto as_array()  const -> const Array&  { return std::get<Array>(*this); }
    auto as_object() const -> const Object& { return std::get<Object>(*this); }

    auto& operator[](const std::string& key) {
        return std::get<Object>(*this)[key];
    }
    auto operator[](const std::string& key) const -> const Value& {
        return std::get<Object>(*this).at(key);
    }
    auto operator[](std::size_t i) const -> const Value& {
        return std::get<Array>(*this).at(i);
    }

    bool has(const std::string& key) const {
        if (!is_object()) return false;
        return as_object().count(key) > 0;
    }

    auto get_string(const std::string& key, const std::string& def = "") const -> std::string {
        if (has(key) && (*this)[key].is_string()) return (*this)[key].as_string();
        return def;
    }
    auto get_int(const std::string& key, Int def = 0) const -> Int {
        if (has(key) && (*this)[key].is_int()) return (*this)[key].as_int();
        return def;
    }
    auto get_array(const std::string& key) const -> const Array& {
        static const Array empty;
        if (has(key) && (*this)[key].is_array()) return (*this)[key].as_array();
        return empty;
    }
};

// ═══ Parser ═══

class Parser {
public:
    explicit Parser(std::string_view input) : src_(input), pos_(0) {}

    auto parse() -> Value {
        skip_ws();
        auto v = parse_value();
        return v;
    }

private:
    std::string_view src_;
    std::size_t pos_;

    auto peek() -> char {
        if (pos_ >= src_.size()) return '\0';
        return src_[pos_];
    }
    auto advance() -> char {
        return src_[pos_++];
    }
    void expect(char c) {
        if (peek() != c) throw std::runtime_error(std::string("Expected '") + c + "'");
        advance();
    }
    void skip_ws() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    auto parse_value() -> Value {
        skip_ws();
        char c = peek();
        if (c == '"') return parse_string_val();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        throw std::runtime_error(std::string("Unexpected char: ") + c);
    }

    auto parse_string_val() -> Value {
        return String(parse_string());
    }

    auto parse_string() -> std::string {
        expect('"');
        std::string result;
        while (pos_ < src_.size()) {
            char c = advance();
            if (c == '"') return result;
            if (c == '\\') {
                char esc = advance();
                switch (esc) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        // Parse 4 hex digits, emit as UTF-8
                        std::string hex;
                        for (int i = 0; i < 4; ++i) hex += advance();
                        auto cp = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
                        if (cp < 0x80) {
                            result += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            result += static_cast<char>(0xC0 | (cp >> 6));
                            result += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            result += static_cast<char>(0xE0 | (cp >> 12));
                            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: result += esc;
                }
            } else {
                result += c;
            }
        }
        throw std::runtime_error("Unterminated string");
    }

    auto parse_number() -> Value {
        std::size_t start = pos_;
        bool is_float = false;
        if (peek() == '-') advance();
        while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') advance();
        if (pos_ < src_.size() && src_[pos_] == '.') { is_float = true; advance(); }
        while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') advance();
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            is_float = true; advance();
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-')) advance();
            while (pos_ < src_.size() && src_[pos_] >= '0' && src_[pos_] <= '9') advance();
        }
        auto num_str = std::string(src_.substr(start, pos_ - start));
        if (is_float) {
            return std::stod(num_str);
        } else {
            return static_cast<Int>(std::stoll(num_str));
        }
    }

    auto parse_bool() -> Value {
        if (src_.substr(pos_, 4) == "true")  { pos_ += 4; return true; }
        if (src_.substr(pos_, 5) == "false") { pos_ += 5; return false; }
        throw std::runtime_error("Invalid bool");
    }

    auto parse_null() -> Value {
        if (src_.substr(pos_, 4) == "null") { pos_ += 4; return nullptr; }
        throw std::runtime_error("Invalid null");
    }

    auto parse_array() -> Value {
        expect('[');
        Array arr;
        skip_ws();
        if (peek() == ']') { advance(); return arr; }
        while (true) {
            arr.push_back(parse_value());
            skip_ws();
            if (peek() == ',') { advance(); continue; }
            break;
        }
        expect(']');
        return arr;
    }

    auto parse_object() -> Value {
        expect('{');
        Object obj;
        skip_ws();
        if (peek() == '}') { advance(); return obj; }
        while (true) {
            skip_ws();
            auto key = parse_string();
            skip_ws();
            expect(':');
            obj[key] = parse_value();
            skip_ws();
            if (peek() == ',') { advance(); continue; }
            break;
        }
        expect('}');
        return obj;
    }
};

inline auto parse(std::string_view input) -> Value {
    return Parser(input).parse();
}

// ═══ Serializer ═══

inline void serialize(const Value& v, std::string& out) {
    if (v.is_null())   { out += "null"; return; }
    if (v.is_bool())   { out += v.as_bool() ? "true" : "false"; return; }
    if (v.is_int())    { out += std::to_string(v.as_int()); return; }
    if (v.is_double()) {
        std::ostringstream ss;
        ss << v.as_double();
        out += ss.str();
        return;
    }
    if (v.is_string()) {
        out += '"';
        for (char c : v.as_string()) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:   out += c;
            }
        }
        out += '"';
        return;
    }
    if (v.is_array()) {
        out += '[';
        bool first = true;
        for (auto& item : v.as_array()) {
            if (!first) out += ',';
            first = false;
            serialize(item, out);
        }
        out += ']';
        return;
    }
    if (v.is_object()) {
        out += '{';
        bool first = true;
        for (auto& [key, val] : v.as_object()) {
            if (!first) out += ',';
            first = false;
            out += '"';
            for (char c : key) {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            out += "\":";
            serialize(val, out);
        }
        out += '}';
        return;
    }
}

inline auto serialize(const Value& v) -> std::string {
    std::string out;
    serialize(v, out);
    return out;
}

} // namespace arisa::json