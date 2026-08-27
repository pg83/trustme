#pragma once

// Invocation-local state shared by all procedural macro subprocesses.
class ProcMacroContext {
    unsigned nextDump = 0;

public:
    unsigned newDumpIndex() {
        return nextDump++;
    }
};
