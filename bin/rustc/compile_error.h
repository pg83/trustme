#pragma once

#include <exception>
#include <string>

class TokenStream;


    class CompileErrorBase: public ::std::exception {
    public:
        virtual ~CompileErrorBase() throw();
    };

    class CompileErrorGeneric: public CompileErrorBase {
        ::std::string message;

    public:
        CompileErrorGeneric(::std::string message);
        CompileErrorGeneric(const TokenStream& lex, ::std::string message);

        virtual ~CompileErrorGeneric() throw();
    };

    class CompileErrorBugCheck: public CompileErrorBase {
        ::std::string message;

    public:
        CompileErrorBugCheck(::std::string message);
        CompileErrorBugCheck(const TokenStream& lex, ::std::string message);

        virtual ~CompileErrorBugCheck() throw();
    };

    class CompileErrorTodo: public CompileErrorBase {
        ::std::string message;

    public:
        CompileErrorTodo(::std::string message);
        CompileErrorTodo(const TokenStream& lex, ::std::string message);
        virtual ~CompileErrorTodo() throw();
    };

