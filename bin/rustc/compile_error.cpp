#include "compile_error.h"

#include "parse_tokenstream.h"

#include <iostream>

void compileErrorGeneric(const char* message) {
    std::cout << "Generic(" << message << ")" << std::endl;
    std::abort();
}

void compileErrorGeneric(const TokenStream& lex, const char* message) {
    std::cout << lex.pointSpan() << ": Generic(" << message << ")" << std::endl;
    std::abort();
}

void compileErrorBugCheck(const char* message) {
    std::cout << "BugCheck(" << message << ")" << std::endl;
    std::abort();
}

void compileErrorBugCheck(const TokenStream& lex, const char* message) {
    std::cout << lex.pointSpan() << "BugCheck(" << message << ")" << std::endl;
    std::abort();
}

void compileErrorTodo(const char* message) {
    std::cout << "Todo(" << message << ")" << std::endl;
    std::abort();
}

void compileErrorTodo(const TokenStream& lex, const char* message) {
    std::cout << lex.pointSpan() << ": Todo(" << message << ")" << std::endl;
    std::abort();
}
