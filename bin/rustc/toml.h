#pragma once

#include <std/sys/types.h>

/*
 * A very basic (and probably incomplete) streaming TOML parser
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

    std::ifstream input;

    std::string filename;
    unsigned line;

protected:
    TomlLexer(const std::string& filename);
    TomlToken getToken();

public:
    friend std::ostream& operator<<(std::ostream& os, const TomlLexer& x);
};

class TomlFile {
    TomlLexer lexer_;

    std::vector<std::string> currentBlock;

    std::vector<std::vector<std::string>> currentComposite;

    unsigned int nextArrayIndex;

    std::unordered_map<std::string, unsigned> arrayCounts;

public:
    TomlFile(const std::string& filename);

    TomlFileIter begin();
    TomlFileIter end();

    TomlKeyValue getNextValue();

    const TomlLexer& lexer() const {
        return lexer_;
    }

private:
    std::vector<std::string> getPath(std::vector<std::string> tail) const;

    void skipCompositeValue();
};

struct TomlValue {
    enum class Type {
        Boolean,

        String,

        Integer,

        List,
    };

    friend std::ostream& operator<<(std::ostream& os, const Type& e);

    struct TypeError: public std::exception {
        Type have;
        Type exp;

        char message[64];

        TypeError(Type h, Type e);

        const char* what() const noexcept override;

        friend std::ostream& operator<<(std::ostream& os, const TypeError& e);
    };

    Type type;
    u64 intValue;
    std::string strValue;
    std::vector<TomlValue> subValues;

    TomlValue();

    TomlValue(std::string s);

    TomlValue(i64 v);

    TomlValue(bool v);

    const std::string& asString() const;

    bool asBool() const;

    u64 asInt() const;

    const std::vector<TomlValue>& asList() const;

    friend std::ostream& operator<<(std::ostream& os, const TomlValue& x);
};

struct TomlKeyValue {
    typedef std::vector<std::string> Path;

    // TODO: How are things like `[[bin]]` handled?
    Path path;

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
