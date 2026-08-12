#pragma once

/*
 * mrustc common tools
 * - by John Hodge (Mutabah)
 *
 * tools/common/toml.h
 * - A very basic (and probably incomplete) streaming TOML parser
 */

#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

class TomlFileIter;
struct TomlKeyValue;

struct TomlToken;

class TomlLexer {
    friend class TomlFile;
    /// Input file stream
    ::std::ifstream input;

    ::std::string filename;
    unsigned line;

protected:
    TomlLexer(const ::std::string& filename);
    TomlToken getToken();

public:
    friend ::std::ostream& operator<<(::std::ostream& os, const TomlLexer& x);
};

class TomlFile {
    /// Input file stream
    TomlLexer mLexer;

    /// Name of the current `[]` block
    ::std::vector<::std::string> currentBlock;

    /// Path suffix of the current composite (none if empty)
    ::std::vector<std::vector<std::string>> currentComposite;

    /// Index of the next array field (if zero, not parsing an array)
    unsigned int nextArrayIndex;

    /// Next indexes if top-level defined arrays (e.g. `[[foo]]`)
    ::std::unordered_map<::std::string, unsigned> arrayCounts;

public:
    TomlFile(const ::std::string& filename);

    TomlFileIter begin();
    TomlFileIter end();

    // Obtain the next value in the file
    TomlKeyValue getNextValue();

    const TomlLexer& lexer() const {
        return mLexer;
    }

private:
    std::vector<std::string> getPath(std::vector<std::string> tail) const;
    /// Consume a balanced `[...]` / `{...}` group (nested groups included),
    /// discarding its contents. The opening bracket/brace must already have
    /// been consumed. Used to skip nested arrays / inline tables that only
    /// appear in sections minicargo never reads (e.g. `[package.metadata.*]`).
    void skipCompositeValue();
};

struct TomlValue {
    enum class Type {
        // A true/false, 1/0, yes/no value
        Boolean,
        // A double-quoted string
        String,
        // Integer
        Integer,
        // A list of other values
        List,
    };

    friend ::std::ostream& operator<<(::std::ostream& os, const Type& e);

    struct TypeError: public ::std::exception {
        Type have;
        Type exp;

        TypeError(Type h, Type e);

        const char* what() const noexcept override {
            return "toml type error";
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const TypeError& e);
    };

    Type mType;
    uint64_t intValue;
    ::std::string strValue;
    ::std::vector<TomlValue> subValues;

    TomlValue();

    TomlValue(::std::string s);

    TomlValue(int64_t v);

    TomlValue(bool v);

    const ::std::string& asString() const;

    bool asBool() const;

    uint64_t asInt() const;

    const ::std::vector<TomlValue>& asList() const;

    friend ::std::ostream& operator<<(::std::ostream& os, const TomlValue& x);
};

struct TomlKeyValue {
    typedef ::std::vector<::std::string> Path;
    // Path to the value (last node is the value name)
    // TODO: How are things like `[[bin]]` handled?
    Path path;
    // Relevant value
    TomlValue value;
};

class TomlFileIter {
    friend class TomlFile;
    TomlFile& reader;
    TomlKeyValue curValue;

    TomlFileIter(TomlFile& tf);

public:
    TomlKeyValue operator*() const {
        return curValue;
    }

    void operator++() {
        curValue = reader.getNextValue();
    }

    bool operator!=(const TomlFileIter& x) const {
        return curValue.path != x.curValue.path;
    }
};
