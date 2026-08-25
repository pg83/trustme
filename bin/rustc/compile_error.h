#pragma once

class TokenStream;

// Fatal compile-error reporters: print and abort at the call site. These were
// exception classes whose constructors already did the reporting; the throw
// never carried information anywhere.
[[noreturn]] void compileErrorGeneric(const char* message);
[[noreturn]] void compileErrorGeneric(const TokenStream& lex, const char* message);
[[noreturn]] void compileErrorBugCheck(const char* message);
[[noreturn]] void compileErrorBugCheck(const TokenStream& lex, const char* message);
[[noreturn]] void compileErrorTodo(const char* message);
[[noreturn]] void compileErrorTodo(const TokenStream& lex, const char* message);
