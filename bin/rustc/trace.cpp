#include "trace.h"

#if defined(TRUSTME_DEBUG)

    #include <iostream>

std::ostream& traceOutput(const char* function) {
    return std::cout << function << ": ";
}

TraceLog::TraceLog(const char* function)
    : function(function)
{
    traceOutput(function) << ">>\n";
}

TraceLog::~TraceLog() {
    traceOutput(function) << "<< ()\n";
}

#endif
