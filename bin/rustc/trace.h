#pragma once

#include "output.h"

#if defined(TRUSTME_DEBUG)

stl::OutBuf traceOutput(const char* function);

struct TraceLog {
    const char* function;

    explicit TraceLog(const char* function);

    template <typename WriteInput>
    TraceLog(const char* function, WriteInput writeInput)
        : function(function)
    {
        auto out = traceOutput(function);
        out << stl::StringView(">> (");
        writeInput(out);
        out << stl::StringView(")\n");
    }

    ~TraceLog();
};

template <typename WriteOutput>
struct TraceLogWithOutput {
    const char* function;
    WriteOutput writeOutput;

    template <typename WriteInput>
    TraceLogWithOutput(const char* function, WriteInput writeInput, WriteOutput writeOutput)
        : function(function)
        , writeOutput(writeOutput)
    {
        auto out = traceOutput(function);
        out << stl::StringView(">> (");
        writeInput(out);
        out << stl::StringView(")\n");
    }

    ~TraceLogWithOutput() {
        auto out = traceOutput(function);
        out << stl::StringView("<< (");
        writeOutput(out);
        out << stl::StringView(")\n");
    }
};

template <typename WriteInput, typename WriteOutput>
auto makeTraceLogWithOutput(const char* function, WriteInput writeInput, WriteOutput writeOutput) {
    return TraceLogWithOutput<WriteOutput>(function, writeInput, writeOutput);
}

    #define DEBUG(...)                                    \
        do {                                              \
            traceOutput(__func__) << __VA_ARGS__ << '\n'; \
        } while (0)
    #define TRACE_FUNCTION TraceLog traceFunction(__func__)
    #define TRACE_FUNCTION_F(...)                                 \
        TraceLog traceFunction(__func__, [&](auto& traceStream) { \
            traceStream << __VA_ARGS__;                           \
        })
    #define TRACE_FUNCTION_FR(input, output)                                           \
        auto traceFunction = makeTraceLogWithOutput(__func__, [&](auto& traceStream) { \
            traceStream << input;                                                      \
        }, [&](auto& traceStream) {                                                    \
            traceStream << output;                                                     \
        })

#else

    #define DEBUG(...)
    #define TRACE_FUNCTION
    #define TRACE_FUNCTION_F(...)
    #define TRACE_FUNCTION_FR(...)

#endif
