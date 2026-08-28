#pragma once

class TokenStream;

[[noreturn]] void compileErrorGeneric(const char* message);
[[noreturn]] void compileErrorGeneric(const TokenStream& lex, const char* message);
[[noreturn]] void compileErrorBugCheck(const char* message);
[[noreturn]] void compileErrorBugCheck(const TokenStream& lex, const char* message);
[[noreturn]] void compileErrorBugCheckAt(const char* file, int line, const char* condition);
[[noreturn]] void compileErrorTodo(const char* message);
[[noreturn]] void compileErrorTodo(const TokenStream& lex, const char* message);

#define BUG_ASSERT(cnd)                                       \
    do {                                                      \
        if (!(cnd)) {                                         \
            compileErrorBugCheckAt(__FILE__, __LINE__, #cnd); \
        }                                                     \
    } while (0)
