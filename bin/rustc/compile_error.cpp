#include "compile_error.h"

#include "parse_tokenstream.h"


using namespace stl;

void compileErrorGeneric(const char* message) {
    sysO << StringView("Generic(") << message << StringView(")") << endL;
    std::abort();
}

void compileErrorGeneric(const TokenStream& lex, const char* message) {
    sysO << lex.pointSpan() << StringView(": Generic(") << message << StringView(")") << endL;
    std::abort();
}

void compileErrorBugCheck(const char* message) {
    sysO << StringView("BugCheck(") << message << StringView(")") << endL;
    std::abort();
}

void compileErrorBugCheck(const TokenStream& lex, const char* message) {
    sysO << lex.pointSpan() << StringView("BugCheck(") << message << StringView(")") << endL;
    std::abort();
}

void compileErrorBugCheckAt(const char* file, int line, const char* condition) {
    sysE << file << StringView(":") << line << StringView(": BUG: assertion failed: ") << condition << endL;
    std::abort();
}

void compileErrorTodo(const char* message) {
    sysO << StringView("Todo(") << message << StringView(")") << endL;
    std::abort();
}

void compileErrorTodo(const TokenStream& lex, const char* message) {
    sysO << lex.pointSpan() << StringView(": Todo(") << message << StringView(")") << endL;
    std::abort();
}
