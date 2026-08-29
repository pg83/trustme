/*
 * A very basic (and probably incomplete) streaming TOML parser
 */
#include "toml.h"
#include "output.h"
#include "common.h"

#include <cstdio>
#include <string>

using namespace stl;

struct TomlLexer::TomlToken {
    enum class Type {
        Eof,
        SquareOpen,
        SquareClose,
        BraceOpen,
        BraceClose,
        Assign,
        Newline,
        Comma,
        Dot,

        Ident,
        String,
        Integer,
    };

    Type type;
    std::string data;
    i64 intval = 0;

    TomlToken(Type ty);

    TomlToken(Type ty, std::string s);

    TomlToken(Type ty, i64 i);

    static TomlToken lexFrom(std::ifstream& is, unsigned& line);
    static TomlToken lexFromInner(std::ifstream& is, unsigned& line);

    const std::string& asString() const;

    friend ZeroCopyOutput& operator<<(ZeroCopyOutput& os, const TomlToken& x) {
        switch (x.type) {
            case Type::Eof:
                os << StringView("Eof");
                break;
            case Type::SquareOpen:
                os << StringView("SquareOpen");
                break;
            case Type::SquareClose:
                os << StringView("SquareClose");
                break;
            case Type::BraceOpen:
                os << StringView("BraceOpen");
                break;
            case Type::BraceClose:
                os << StringView("BraceClose");
                break;
            case Type::Assign:
                os << StringView("Assign");
                break;
            case Type::Newline:
                os << StringView("Newline");
                break;
            case Type::Comma:
                os << StringView("Comma");
                break;
            case Type::Dot:
                os << StringView("Dot");
                break;
            case Type::Ident:
                os << StringView("Ident(") << x.data << StringView(")");
                break;
            case Type::String:
                os << StringView("String(") << x.data << StringView(")");
                break;
            case Type::Integer:
                os << StringView("Integer(") << x.intval << StringView(")");
                break;
        }
        return os;
    }
};

TomlFile::TomlFile(const std::string& filename)
    : lexer_(filename)
{
}

TomlFileIter TomlFile::begin() {
    TomlFileIter rv{*this};
    ++rv;
    return rv;
}

TomlFileIter TomlFile::end() {
    return TomlFileIter{*this};
}

TomlKeyValue TomlFile::getNextValue() {
    using TomlToken = TomlLexer::TomlToken;

    auto t = lexer_.getToken();

    if (currentComposite.empty()) {
        while (t.type == TomlToken::Type::Newline) {
            t = lexer_.getToken();
        }

        switch (t.type) {
            case TomlToken::Type::Eof:
                return TomlKeyValue{};
            case TomlToken::Type::SquareOpen: {
                currentBlock.clear();

                t = lexer_.getToken();
                bool isArray = false;
                if (t.type == TomlToken::Type::SquareOpen) {
                    isArray = true;
                    t = lexer_.getToken();
                }
                for (;;) {
                    if (!(t.type == TomlToken::Type::Ident || t.type == TomlToken::Type::String)) {
                        throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected token in block name - ") << t));
                    }
                    currentBlock.push_back(t.asString());

                    t = lexer_.getToken();
                    if (t.type != TomlToken::Type::Dot) {
                        break;
                    }
                    t = lexer_.getToken();
                }
                if (isArray) {
                    currentBlock.push_back(FMT(arrayCounts[currentBlock.back()]++));
                    if (t.type != TomlToken::Type::SquareClose) {
                        throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected token after array name - ") << t));
                    }
                    t = lexer_.getToken();
                }
                if (t.type != TomlToken::Type::SquareClose) {
                    throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected token in block header - ") << t));
                }
                t = lexer_.getToken();
                if (t.type != TomlToken::Type::Newline) {
                    throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected token after block block - ") << t));
                }
                DEBUG(StringView("Start block ") << currentBlock);
                return getNextValue();
            }
            default:
                break;
        }
    } else {
        if (t.type == TomlToken::Type::Eof) {
            throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected EOF in composite")));
        }
    }
    std::vector<std::string> keyName;
    for (;;) {
        switch (t.type) {
            case TomlToken::Type::String:
            case TomlToken::Type::Ident:
                break;
            default:
                throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected token for key - ") << t));
        }
        keyName.push_back(t.asString());
        t = lexer_.getToken();
        if (t.type == TomlToken::Type::Assign) {
            break;
        }

        if (t.type != TomlToken::Type::Dot) {
            throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected token after key - ") << t));
        }
        t = lexer_.getToken();
    }

    BUG_ASSERT(t.type == TomlToken::Type::Assign);
    t = lexer_.getToken();

    TomlKeyValue rv;
    switch (t.type) {
        case TomlToken::Type::String:
            rv.path = this->getPath(std::move(keyName));
            rv.value = TomlValue{t.data};
            break;
        case TomlToken::Type::SquareOpen: {
            rv.path = this->getPath(std::move(keyName));
            rv.value.type = TomlValue::Type::List;
            bool skippedNested = false;
            while ((t = lexer_.getToken()).type != TomlToken::Type::SquareClose) {
                while (t.type == TomlToken::Type::Newline) {
                    t = lexer_.getToken();
                }
                if (t.type == TomlToken::Type::SquareClose) {
                    break;
                }

                switch (t.type) {
                    case TomlToken::Type::String:
                        rv.value.subValues.push_back(TomlValue{t.asString()});
                        break;
                    case TomlToken::Type::Integer:
                        rv.value.subValues.push_back(TomlValue{t.intval});
                        break;
                    case TomlToken::Type::Ident:
                        if (t.data == "true" || t.data == "false") {
                            rv.value.subValues.push_back(TomlValue{t.data == "true"});
                        } else {
                            throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected identifier in array value position - ") << t));
                        }
                        break;
                    case TomlToken::Type::SquareOpen:
                    case TomlToken::Type::BraceOpen:
                        skippedNested = true;
                        this->skipCompositeValue();
                        break;
                    default:
                        throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected token in array value position - ") << t));
                }

                t = lexer_.getToken();
                if (t.type != TomlToken::Type::Comma) {
                    break;
                }
            }
            while (t.type == TomlToken::Type::Newline) {
                t = lexer_.getToken();
            }
            if (t.type != TomlToken::Type::SquareClose) {
                throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected token after array - ") << t));
            }
            if (skippedNested) {
                std::string key;
                for (const auto& c : rv.path) {
                    if (!key.empty()) {
                        key += ".";
                    }
                    key += c;
                }
                sysE << StringView("warning: ") << lexer_ << StringView(": skipped nested array / inline-table element(s) in `") << key << StringView("` (not represented in the flat TOML value model)") << endL;
            }
            break;
        }
        case TomlToken::Type::BraceOpen:
            currentComposite.push_back(std::move(keyName));
            DEBUG(StringView("Enter composite block ") << currentBlock << StringView(", ") << currentComposite);
            return getNextValue();
        case TomlToken::Type::Integer:
            rv.path = this->getPath(std::move(keyName));
            rv.value = TomlValue{t.intval};
            break;
        case TomlToken::Type::Ident:
            if (t.data == "true") {
                rv.path = this->getPath(std::move(keyName));
                rv.value = TomlValue{true};
            } else if (t.data == "false") {
                rv.path = this->getPath(std::move(keyName));

                rv.value = TomlValue{false};
            } else {
                throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected identifier in value position - ") << t));
            }
            break;
        default:
            throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected token in value position - ") << t));
    }

    t = lexer_.getToken();
    while (!currentComposite.empty() && t.type == TomlToken::Type::BraceClose) {
        DEBUG(StringView("Leave composite block ") << currentBlock << StringView(", ") << currentComposite);
        currentComposite.pop_back();
        t = lexer_.getToken();
    }
    if (currentComposite.empty()) {
        if (t.type != TomlToken::Type::Newline && t.type != TomlToken::Type::Eof) {
            throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected token in TOML file after entry - ") << t));
        }
    } else {
        if (t.type != TomlToken::Type::Comma) {
            throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected token in TOML file after composite entry - ") << t));
        }
    }
    return rv;
}

void TomlFile::skipCompositeValue() {
    using TomlToken = TomlLexer::TomlToken;

    unsigned depth = 1;
    while (depth > 0) {
        auto t = lexer_.getToken();
        switch (t.type) {
            case TomlToken::Type::Eof:
                throw std::runtime_error(FMT(lexer_ << StringView(": Unexpected EOF in nested array/table value")));
            case TomlToken::Type::SquareOpen:
            case TomlToken::Type::BraceOpen:
                depth++;
                break;
            case TomlToken::Type::SquareClose:
            case TomlToken::Type::BraceClose:
                depth--;
                break;
            default:
                break;
        }
    }
}

std::vector<std::string> TomlFile::getPath(std::vector<std::string> tail) const {
    std::vector<std::string> path;
    path = currentBlock;
    for (const auto& compositeEnt : currentComposite) {
        path.insert(path.end(), compositeEnt.begin(), compositeEnt.end());
    }
    path.insert(path.end(), std::make_move_iterator(tail.begin()), std::make_move_iterator(tail.end()));
    return path;
}

TomlLexer::TomlLexer(const std::string& filename)
    : input(filename)
    , filename(filename)
    , line(1)
{
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open file '" + filename + "'");
    }
}

TomlLexer::TomlToken TomlLexer::getToken() {
    auto rv = TomlToken::lexFrom(input, line);
    if (rv.type == TomlToken::Type::Newline) {
        line++;
    }
    return rv;
}

TomlLexer::TomlToken TomlLexer::TomlToken::lexFrom(std::ifstream& is, unsigned& line) {
    auto rv = TomlToken::lexFromInner(is, line);
    return rv;
}

namespace {
    void handleEscape(std::string& str, std::ifstream& is, unsigned& line) {
        int c = is.get();
        switch (c) {
            case '"':
                str += '"';
                break;
            case '\\':
                str += '\\';
                break;
            case 'n':
                str += '\n';
                break;
            case 't':
                str += '\t';
                break;
            case 'r':
                str += '\r';
                break;
            case 'b':
                str += '\b';
                break;
            case 'f':
                str += '\f';
                break;
            case 'u':
                for (int i = 0; i < 4; i++) {
                    (void)is.get();
                }
                str += '?';
                break;
            case 'U':
                for (int i = 0; i < 8; i++) {
                    (void)is.get();
                }
                str += '?';
                break;
            case '\n':
            case '\r':
            case ' ':
            case '\t': {
                if (c == '\n') {
                    line++;
                }
                int n = is.get();
                while (n != EOF && isspace(n)) {
                    if (n == '\n') {
                        line++;
                    }
                    n = is.get();
                }
                if (n != EOF) {
                    is.putback((char)n);
                }
                break;
            }
            default:
                throw std::runtime_error(FMT(StringView("toml.cpp handle_escape: TODO: Escape sequences in strings - `") << (char)c << StringView("`")));
        }
    }
}

TomlLexer::TomlToken TomlLexer::TomlToken::lexFromInner(std::ifstream& is, unsigned& line) {
    int c;
    do {
        c = is.get();
    } while (c != EOF && c != '\n' && isspace(c));

    std::string str;
    switch (c) {
        case EOF:
            return TomlToken{Type::Eof};
        case '[':
            return TomlToken{Type::SquareOpen};
        case ']':
            return TomlToken{Type::SquareClose};
        case '{':
            return TomlToken{Type::BraceOpen};
        case '}':
            return TomlToken{Type::BraceClose};
        case ',':
            return TomlToken{Type::Comma};
        case '.':
            return TomlToken{Type::Dot};
        case '=':
            return TomlToken{Type::Assign};
        case '\n':
            return TomlToken{Type::Newline};
        case '#':
            while (c != '\n') {
                c = is.get();
                if (c == EOF) {
                    return TomlToken{Type::Eof};
                }
            }
            return TomlToken{Type::Newline};
        case '\'':
            c = is.get();
            if (c == '\'') {
                c = is.get();
                if (c != '\'') {
                    str = "";
                } else {
                    c = is.get();
                    if (c == '\n') {
                        line++;
                        c = is.get();
                    }
                    for (;;) {
                        if (c == '\'') {
                            c = is.get();
                            if (c == '\'') {
                                c = is.get();
                                if (c == '\'') {
                                    break;
                                }
                                str += '\'';
                            }
                            str += '\'';
                        }
                        if (c == '\n') {
                            line++;
                        }
                        if (c == EOF) {
                            throw std::runtime_error("Unexpected EOF in triple-quoted string");
                        }
                        c = is.get();
                    }
                }
            } else {
                while (c != '\'') {
                    if (c == EOF) {
                        throw std::runtime_error("Unexpected EOF in single-quoted string");
                    }
                    if (c == '\n') {
                        line++;
                    }
                    str += (char)c;
                    c = is.get();
                }
            }
            return TomlToken{Type::String, str};
        case '"':
            c = is.get();
            if (c == '"') {
                c = is.get();
                if (c != '"') {
                    is.putback(c);
                    return TomlToken{Type::String, ""};
                } else {
                    c = is.get();
                    if (c == '\n') {
                        line++;
                        c = is.get();
                    }
                    for (;;) {
                        if (c == '"') {
                            c = is.get();
                            if (c == '"') {
                                c = is.get();
                                if (c == '"') {
                                    break;
                                }
                                str += '"';
                            }
                            str += '"';
                        }
                        if (c == EOF) {
                            throw std::runtime_error("Unexpected EOF in triple-quoted string");
                        }
                        if (c == '\\') {
                            handleEscape(str, is, line);
                        } else {
                            str += (char)c;
                            if (c == '\n') {
                                line++;
                            }
                        }
                        c = is.get();
                    }
                }
            } else {
                while (c != '"') {
                    if (c == EOF) {
                        throw std::runtime_error("Unexpected EOF in double-quoted string");
                    }
                    if (c == '\\') {
                        handleEscape(str, is, line);
                        c = is.get();
                        continue;
                    }
                    if (c == '\n') {
                        line++;
                    }
                    str += (char)c;
                    c = is.get();
                }
            }
            return TomlToken{Type::String, str};
        default:
            if (isalnum(c) || c == '_' || c == '-') {
                while (isalnum(c) || c == '-' || c == '_') {
                    str += (char)c;
                    c = is.get();
                }
                is.putback(c);

                i64 val = 0;
                bool isAllDigit = true;
                bool isNeg = false;
                size_t i = 0;
                if (str[0] == '-') {
                    isNeg = true;
                    i++;
                }

                if (str.size() - i > 2 && str[i] == '0') {
                    if (str[i + 1] == 'x') {
                        i += 2;
                        for (; i < str.size(); i++) {
                            c = str[i];
                            if (!isxdigit(c)) {
                                isAllDigit = false;
                                break;
                            }
                            val *= 16;
                            val += (c <= '9' ? c - '0' : (c & ~0x20) - 'A' + 10);
                        }
                    } else if (str[i + 1] == 'o') {
                        i += 2;
                        for (; i < str.size(); i++) {
                            c = str[i];
                            if (!('0' <= c && c <= '7')) {
                                isAllDigit = false;
                                break;
                            }
                            val *= 8;
                            val += c - '0';
                        }
                    } else if (str[i + 1] == 'b') {
                        i += 2;
                        for (; i < str.size(); i++) {
                            c = str[i];
                            if (!('0' <= c && c <= '1')) {
                                isAllDigit = false;
                                break;
                            }
                            val *= 2;
                            val += c - '0';
                        }
                    } else {
                    }
                } else {
                    for (; i < str.size(); i++) {
                        c = str[i];
                        if (!isdigit(c)) {
                            isAllDigit = false;
                            break;
                        }
                        val *= 10;
                        val += c - '0';
                    }
                }
                if (isAllDigit) {
                    return TomlToken{Type::Integer, (isNeg ? -val : val)};
                }
                return TomlToken{Type::Ident, str};
            } else {
                throw std::runtime_error(FMT(StringView("?:") << line << StringView(": Unexpected character '") << (char)c << StringView("' in file")));
            }
    }
}

TomlValue::TomlValue()
    : type(Type::String)
    , intValue(0)
{
}

TomlValue::TomlValue(std::string s)
    : type(Type::String)
    , intValue(0)
    , strValue(std::move(s))
{
}

TomlValue::TomlValue(i64 v)
    : type(Type::Integer)
    , intValue(v)
{
}

TomlValue::TomlValue(bool v)
    : type(Type::Boolean)
    , intValue(v ? 1 : 0)
{
}

const std::string& TomlValue::asString() const {
    if (type != Type::String) {
        throw TypeError{type, Type::String};
    }
    return strValue;
}

bool TomlValue::asBool() const {
    if (type != Type::Boolean) {
        throw TypeError{type, Type::Boolean};
    }
    return intValue != 0;
}

u64 TomlValue::asInt() const {
    if (type != Type::Integer) {
        throw TypeError{type, Type::Integer};
    }
    return intValue;
}

const std::vector<TomlValue>& TomlValue::asList() const {
    if (type != Type::List) {
        throw TypeError{type, Type::List};
    }
    return subValues;
}

TomlFileIter::TomlFileIter(TomlFile& tf)
    : reader(tf)
{
}

TomlValue::TypeError::TypeError(TomlValue::Type h, TomlValue::Type e)
    : have(h)
    , exp(e)
{
    StringBuilder ss;
    ss << StringView("TOML type error: ") << *this;
    const std::string rendered(static_cast<const char*>(ss.data()), ss.length());
    snprintf(message, sizeof(message), "%s", rendered.c_str());
}

const char* TomlValue::TypeError::what() const noexcept {
    return message;
}

TomlLexer::TomlToken::TomlToken(Type ty)
    : type(ty)
{
}

TomlLexer::TomlToken::TomlToken(Type ty, std::string s)
    : type(ty)
    , data(s)
{
}

TomlLexer::TomlToken::TomlToken(Type ty, i64 i)
    : type(ty)
    , intval(i)
{
}

auto TomlLexer::TomlToken::asString() const -> const std::string& {
    BUG_ASSERT(type == Type::Ident || type == Type::String);
    return data;
}

template <>
void stl::output<ZeroCopyOutput, TomlLexer::TomlToken>(ZeroCopyOutput& os, const TomlLexer::TomlToken& value) {
    operator<<(os, value);
}

template <>
void stl::output<ZeroCopyOutput, TomlLexer>(ZeroCopyOutput& os, const TomlLexer& x) {
    os << x.getFilename() << StringView(":") << x.getLine();
}

template <>
void stl::output<ZeroCopyOutput, TomlValue::Type>(ZeroCopyOutput& os, TomlValue::Type e) {
    switch (e) {
        case TomlValue::Type::Boolean:
            os << StringView("boolean");
            break;
        case TomlValue::Type::String:
            os << StringView("string");
            break;
        case TomlValue::Type::Integer:
            os << StringView("integer");
            break;
        case TomlValue::Type::List:
            os << StringView("list");
            break;
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, TomlValue>(ZeroCopyOutput& os, const TomlValue& x) {
    switch (x.type) {
        case TomlValue::Type::Boolean:
            os << (x.intValue != 0 ? "true" : "false");
            break;
        case TomlValue::Type::Integer:
            os << x.intValue;
            break;
        case TomlValue::Type::List:
            os << StringView("[");
            for (auto& e : x.subValues) {
                os << e << StringView(",");
            }
            os << StringView("]");
            break;
        case TomlValue::Type::String:
            os << StringView("\"");
            for (u8 c : x.strValue) {
                switch (c) {
                    case '\n':
                        os << StringView("\\n");
                        break;
                    case '\r':
                        os << StringView("\\n");
                        break;
                    case '\t':
                        os << StringView("\\t");
                        break;
                    default:
                        if (0x20 <= c && c <= 0x7F) {
                            os << static_cast<char>(c);
                        } else {
                            static const char* H = "0123456789ABCDEF";
                            os << StringView("\\x") << H[c >> 4] << H[c & 0xF];
                        }
                }
            }
            os << StringView("\"");
            break;
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, TomlValue::TypeError>(ZeroCopyOutput& os, const TomlValue::TypeError& e) {
    os << StringView("expected ") << e.exp << StringView(", got ") << e.have;
    return;
}
