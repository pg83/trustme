#include "trace.h"

#if defined(TRUSTME_DEBUG)

using namespace stl;

OutBuf traceOutput(const char* function) {
    auto out = sysO;
    out << function << StringView(": ");
    return out;
}

TraceLog::TraceLog(const char* function)
    : function(function)
{
    traceOutput(function) << StringView(">>\n");
}

TraceLog::~TraceLog() {
    traceOutput(function) << StringView("<< ()\n");
}

#endif
