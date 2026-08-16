#pragma once

/*
 */
#include <ctime>
#include <initializer_list>

extern void debugInitPhases(const char* envVarName, std::initializer_list<const char*> il);

class DebugTimedPhase {
    const char* name_;
    clock_t start;

public:
    DebugTimedPhase(const char* name);
    ~DebugTimedPhase();
};
