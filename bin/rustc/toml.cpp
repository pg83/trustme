/*
 * mrustc common tools
 * - by John Hodge (Mutabah)
 *
 * tools/common/toml.cpp
 * - A very basic (and probably incomplete) streaming TOML parser
 */
#include "toml.h"
#include "common_debug.h"
#include <cassert>
#include <string>
#include <iostream>
#define NOLOG // Disable logging

/// Representation of a syntatic token in a TOML file
struct TomlToken {
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

    Type mType;
    ::std::string mData;
    int64_t intval = 0;

    TomlToken(Type ty)
        : mType(ty)
    {
    }

    TomlToken(Type ty, ::std::string s)
        : mType(ty)
        , mData(s)
    {
    }

    TomlToken(Type ty, int64_t i)
        : mType(ty)
        , intval(i)
    {
    }

    static TomlToken lexFrom(::std::ifstream& is, unsigned& line);
    static TomlToken lexFromInner(::std::ifstream& is, unsigned& line);

    const ::std::string& asString() const {
        assert(mType == Type::Ident || mType == Type::String);
        return mData;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const TomlToken& x) {
        switch (x.mType) {
            case Type::Eof:
                os << "Eof";
                break;
            case Type::SquareOpen:
                os << "SquareOpen";
                break;
            case Type::SquareClose:
                os << "SquareClose";
                break;
            case Type::BraceOpen:
                os << "BraceOpen";
                break;
            case Type::BraceClose:
                os << "BraceClose";
                break;
            case Type::Assign:
                os << "Assign";
                break;
            case Type::Newline:
                os << "Newline";
                break;
            case Type::Comma:
                os << "Comma";
                break;
            case Type::Dot:
                os << "Dot";
                break;
            case Type::Ident:
                os << "Ident(" << x.mData << ")";
                break;
            case Type::String:
                os << "String(" << x.mData << ")";
                break;
            case Type::Integer:
                os << "Integer(" << x.intval << ")";
                break;
        }
        return os;
    }
};

TomlFile::TomlFile(const ::std::string& filename)
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
    auto t = lexer_.getToken();

    if (currentComposite.empty()) {
        while (t.mType == TomlToken::Type::Newline) {
            t = lexer_.getToken();
        }

        // Expect '[', a string, or an identifier
        switch (t.mType) {
            case TomlToken::Type::Eof:
                // Empty return indicates the end of the list
                return TomlKeyValue{};
            case TomlToken::Type::SquareOpen: {
                currentBlock.clear();

                t = lexer_.getToken();
                bool isArray = false;
                if (t.mType == TomlToken::Type::SquareOpen) {
                    isArray = true;
                    t = lexer_.getToken();
                }
                for (;;) {
                    if (!(t.mType == TomlToken::Type::Ident || t.mType == TomlToken::Type::String)) {
                        throw ::std::runtime_error(::format(lexer_, ": Unexpected token in block name - ", t));
                    }
                    currentBlock.push_back(t.asString());

                    t = lexer_.getToken();
                    if (t.mType != TomlToken::Type::Dot) {
                        break;
                    }
                    t = lexer_.getToken();
                }
                if (isArray) {
                    currentBlock.push_back(::format(arrayCounts[currentBlock.back()]++));
                    if (t.mType != TomlToken::Type::SquareClose) {
                        throw ::std::runtime_error(::format(lexer_, ": Unexpected token after array name - ", t));
                    }
                    t = lexer_.getToken();
                }
                if (t.mType != TomlToken::Type::SquareClose) {
                    throw ::std::runtime_error(::format(lexer_, ": Unexpected token in block header - ", t));
                }
                t = lexer_.getToken();
                if (t.mType != TomlToken::Type::Newline) {
                    throw ::std::runtime_error(::format(lexer_, ": Unexpected token after block block - ", t));
                }
                DEBUG("Start block " << currentBlock);
                // Recurse!
                return getNextValue();
            }
            default:
                break;
        }
    } else {
        // Expect a string or an identifier
        if (t.mType == TomlToken::Type::Eof) {
            // EOF isn't allowed here
            throw ::std::runtime_error(::format(lexer_, ": Unexpected EOF in composite"));
        }
    }
    std::vector<std::string> keyName;
    for (;;) {
        switch (t.mType) {
            case TomlToken::Type::String:
            case TomlToken::Type::Ident:
                break;
            default:
                throw ::std::runtime_error(::format(lexer_, ": Unexpected token for key - ", t));
        }
        keyName.push_back(t.asString());
        t = lexer_.getToken();
        if (t.mType == TomlToken::Type::Assign) {
            break;
        }

        if (t.mType != TomlToken::Type::Dot) {
            throw ::std::runtime_error(::format(lexer_, ": Unexpected token after key - ", t));
        }
        t = lexer_.getToken();
    }

    // Note: Should be impossible, as it's the break condition above
    assert(t.mType == TomlToken::Type::Assign);
    t = lexer_.getToken();

    // --- Value ---
    TomlKeyValue rv;
    switch (t.mType) {
        // String: Return the string value
        case TomlToken::Type::String:
            rv.path = this->getPath(std::move(keyName));
            rv.value = TomlValue{t.mData};
            break;
        // Array: Parse the entire list and return as Type::List
        case TomlToken::Type::SquareOpen: {
            rv.path = this->getPath(std::move(keyName));
            rv.value.mType = TomlValue::Type::List;
            bool skippedNested = false;
            while ((t = lexer_.getToken()).mType != TomlToken::Type::SquareClose) {
                while (t.mType == TomlToken::Type::Newline) {
                    t = lexer_.getToken();
                }
                if (t.mType == TomlToken::Type::SquareClose) {
                    break;
                }

                switch (t.mType) {
                    case TomlToken::Type::String:
                        rv.value.subValues.push_back(TomlValue{t.asString()});
                        break;
                    case TomlToken::Type::Integer:
                        rv.value.subValues.push_back(TomlValue{t.intval});
                        break;
                    case TomlToken::Type::Ident:
                        if (t.mData == "true" || t.mData == "false") {
                            rv.value.subValues.push_back(TomlValue{t.mData == "true"});
                        } else {
                            throw ::std::runtime_error(::format(lexer_, ": Unexpected identifier in array value position - ", t));
                        }
                        break;
                    // Nested array or inline table. This parser's value model is flat
                    // (strings / scalars), so these are consumed (balanced) and
                    // discarded rather than aborting the whole file. A warning is
                    // emitted after the loop so a dropped value is noticed if it ever
                    // turns out to matter.
                    case TomlToken::Type::SquareOpen:
                    case TomlToken::Type::BraceOpen:
                        skippedNested = true;
                        this->skipCompositeValue();
                        break;
                    default:
                        throw ::std::runtime_error(::format(lexer_, ": Unexpected token in array value position - ", t));
                }

                t = lexer_.getToken();
                if (t.mType != TomlToken::Type::Comma) {
                    break;
                }
            }
            while (t.mType == TomlToken::Type::Newline) {
                t = lexer_.getToken();
            }
            if (t.mType != TomlToken::Type::SquareClose) {
                throw ::std::runtime_error(::format(lexer_, ": Unexpected token after array - ", t));
            }
            if (skippedNested) {
                ::std::string key;
                for (const auto& c : rv.path) {
                    if (!key.empty()) {
                        key += ".";
                    }
                    key += c;
                }
                ::std::cerr << "warning: " << lexer_ << ": skipped nested array / inline-table element(s) in `" << key << "` (not represented in the flat TOML value model)" << ::std::endl;
            }
            break;
        }
        case TomlToken::Type::BraceOpen:
            currentComposite.push_back(std::move(keyName));
            DEBUG("Enter composite block " << currentBlock << ", " << currentComposite);
            // Recurse to restart parse
            return getNextValue();
        case TomlToken::Type::Integer:
            rv.path = this->getPath(std::move(keyName));
            rv.value = TomlValue{t.intval};
            break;
        case TomlToken::Type::Ident:
            if (t.mData == "true") {
                rv.path = this->getPath(std::move(keyName));
                rv.value = TomlValue{true};
            } else if (t.mData == "false") {
                rv.path = this->getPath(std::move(keyName));

                rv.value = TomlValue{false};
            } else {
                throw ::std::runtime_error(::format(lexer_, ": Unexpected identifier in value position - ", t));
            }
            break;
        default:
            throw ::std::runtime_error(::format(lexer_, ": Unexpected token in value position - ", t));
    }

    t = lexer_.getToken();
    while (!currentComposite.empty() && t.mType == TomlToken::Type::BraceClose) {
        DEBUG("Leave composite block " << currentBlock << ", " << currentComposite);
        currentComposite.pop_back();
        t = lexer_.getToken();
    }
    if (currentComposite.empty()) {
        if (t.mType != TomlToken::Type::Newline && t.mType != TomlToken::Type::Eof) {
            throw ::std::runtime_error(::format(lexer_, ": Unexpected token in TOML file after entry - ", t));
        }
    } else {
        if (t.mType != TomlToken::Type::Comma) {
            throw ::std::runtime_error(::format(lexer_, ": Unexpected token in TOML file after composite entry - ", t));
        }
    }
    return rv;
}

void TomlFile::skipCompositeValue() {
    // The opening `[` or `{` has already been consumed by the caller. Read
    // tokens (including any nested groups) until the matching close balances
    // the count back to zero. Contents are discarded.
    unsigned depth = 1;
    while (depth > 0) {
        auto t = lexer_.getToken();
        switch (t.mType) {
            case TomlToken::Type::Eof:
                throw ::std::runtime_error(::format(lexer_, ": Unexpected EOF in nested array/table value"));
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

TomlLexer::TomlLexer(const ::std::string& filename)
    : input(filename)
    , filename(filename)
    , line(1)
{
    if (!input.is_open()) {
        throw ::std::runtime_error("Unable to open file '" + filename + "'");
    }
}

TomlToken TomlLexer::getToken() {
    auto rv = TomlToken::lexFrom(input, line);
    if (rv.mType == TomlToken::Type::Newline) {
        line++;
    }
    return rv;
}

::std::ostream& operator<<(::std::ostream& os, const TomlLexer& x) {
    os << x.filename << ":" << x.line;
    return os;
}

TomlToken TomlToken::lexFrom(::std::ifstream& is, unsigned& line) {
    auto rv = TomlToken::lexFromInner(is, line);
    return rv;
}

namespace {
    void handleEscape(::std::string& str, ::std::ifstream& is, unsigned& line) {
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
            // `\uXXXX` / `\UXXXXXXXX`: consume the hex digits. minicargo never
            // needs the exact codepoint of a string value, so store a placeholder.
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
            // Line-ending backslash in a multi-line basic string: trim the newline
            // and all following whitespace up to the next non-whitespace char.
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
                throw ::std::runtime_error(format("toml.cpp handle_escape: TODO: Escape sequences in strings - `", (char)c, "`"));
        }
    }
}

TomlToken TomlToken::lexFromInner(::std::ifstream& is, unsigned& line) {
    int c;
    do {
        c = is.get();
    } while (c != EOF && c != '\n' && isspace(c));

    ::std::string str;
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
        // Literal string: No escaping
        case '\'':
            c = is.get();
            if (c == '\'') {
                c = is.get();
                // Empty literal string
                if (c != '\'') {
                    str = "";
                } else {
                    // If the first character is a newline, strip it
                    c = is.get();
                    if (c == '\n') {
                        line++;
                        c = is.get();
                    }
                    // Multi-line literal string
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
                            throw ::std::runtime_error("Unexpected EOF in triple-quoted string");
                        }
                        c = is.get();
                    }
                }
            } else {
                while (c != '\'') {
                    if (c == EOF) {
                        throw ::std::runtime_error("Unexpected EOF in single-quoted string");
                    }
                    // Technically not allowed
                    if (c == '\n') {
                        line++;
                    }
                    str += (char)c;
                    c = is.get();
                }
            }
            return TomlToken{Type::String, str};
        // Basic string: has escape sequences
        case '"':
            c = is.get();
            if (c == '"') {
                c = is.get();
                if (c != '"') {
                    is.putback(c);
                    return TomlToken{Type::String, ""};
                } else {
                    // Strip newline if it's the first character
                    c = is.get();
                    if (c == '\n') {
                        line++;
                        c = is.get();
                    }
                    // Keep reading until """
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
                            throw ::std::runtime_error("Unexpected EOF in triple-quoted string");
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
                        throw ::std::runtime_error("Unexpected EOF in double-quoted string");
                    }
                    if (c == '\\') {
                        handleEscape(str, is, line);
                        c = is.get();
                        continue;
                    }
                    // Technically not allowed
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
                // Identifier
                while (isalnum(c) || c == '-' || c == '_') {
                    str += (char)c;
                    c = is.get();
                }
                is.putback(c);

                int64_t val = 0;
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
                        // Literal `0` is handled below
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
                throw ::std::runtime_error(::format("?:", line, ": Unexpected character '", (char)c, "' in file"));
            }
    }
}

TomlValue::TomlValue()
    : mType(Type::String)
    , intValue(0)
{
}

TomlValue::TomlValue(::std::string s)
    : mType(Type::String)
    , intValue(0)
    , strValue(::std::move(s))
{
}

TomlValue::TomlValue(int64_t v)
    : mType(Type::Integer)
    , intValue(v)
{
}

TomlValue::TomlValue(bool v)
    : mType(Type::Boolean)
    , intValue(v ? 1 : 0)
{
}

const ::std::string& TomlValue::asString() const {
    if (mType != Type::String) {
        throw TypeError{mType, Type::String};
    }
    return strValue;
}

bool TomlValue::asBool() const {
    if (mType != Type::Boolean) {
        throw TypeError{mType, Type::Boolean};
    }
    return intValue != 0;
}

uint64_t TomlValue::asInt() const {
    if (mType != Type::Integer) {
        throw TypeError{mType, Type::Integer};
    }
    return intValue;
}

const ::std::vector<TomlValue>& TomlValue::asList() const {
    if (mType != Type::List) {
        throw TypeError{mType, Type::List};
    }
    return subValues;
}

TomlFileIter::TomlFileIter(TomlFile& tf)
    : reader(tf)
{
}

::std::ostream& operator<<(::std::ostream& os, const TomlValue::Type& e) {
    switch (e) {
        case TomlValue::Type::Boolean:
            os << "boolean";
            break;
        case TomlValue::Type::String:
            os << "string";
            break;
        case TomlValue::Type::Integer:
            os << "integer";
            break;
        case TomlValue::Type::List:
            os << "list";
            break;
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const TomlValue& x) {
    switch (x.mType) {
        case TomlValue::Type::Boolean:
            os << (x.intValue != 0 ? "true" : "false");
            break;
        case TomlValue::Type::Integer:
            os << x.intValue;
            break;
        case TomlValue::Type::List:
            os << "[";
            for (auto& e : x.subValues) {
                os << e << ",";
            }
            os << "]";
            break;
        case TomlValue::Type::String:
            os << "\"";
            for (uint8_t c : x.strValue) {
                switch (c) {
                    case '\n':
                        os << "\\n";
                        break;
                    case '\r':
                        os << "\\n";
                        break;
                    case '\t':
                        os << "\\t";
                        break;
                    default:
                        if (0x20 <= c && c <= 0x7F) {
                            os << c;
                        } else {
                            static const char* H = "0123456789ABCDEF";
                            os << "\\x" << H[c >> 4] << H[c & 0xF];
                        }
                }
            }
            os << "\"";
            break;
    }
    return os;
}

TomlValue::TypeError::TypeError(TomlValue::Type h, TomlValue::Type e)
    : have(h)
    , exp(e)
{
}

::std::ostream& operator<<(::std::ostream& os, const TomlValue::TypeError& e) {
    os << "expected " << e.exp << ", got " << e.have;
    return os;
}

const char* TomlValue::TypeError::what() const noexcept {
    return "toml type error";
}
