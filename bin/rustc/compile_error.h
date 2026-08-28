#pragma once

class TokenStream;

[[noreturn]] void compileErrorGeneric(const char* message);
[[noreturn]] void compileErrorGeneric(const TokenStream& lex, const char* message);
[[noreturn]] void compileErrorBugCheck(const char* message);
[[noreturn]] void compileErrorBugCheck(const TokenStream& lex, const char* message);
[[noreturn]] void compileErrorTodo(const char* message);
[[noreturn]] void compileErrorTodo(const TokenStream& lex, const char* message);
