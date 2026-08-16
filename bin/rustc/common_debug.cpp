/*
 * MiniCargo - mrustc's minimal clone of cargo
 * - By John Hodge (Mutabah/thePowersGang)
 *
 * debug.cpp
 * - Debugging helpers
 */
#include "common_debug.h"
#include <set>
#include <iostream>
#include <mutex>
#include <cstring> // strr

static int giIndentLevel = 0;
static const char* gsDebugPhase = "";
static bool gbDebugPhaseEnabled = false;
static bool gbEnableHeaders = false;
static ::std::set<::std::string> gmDisabledDebug;
static ::std::mutex gDebugLock;

void DebugProcessEnable(const char* e) {
    if (*e) {
        gbEnableHeaders = true;
    }
    while (*e) {
        const char* colon = ::std::strchr(e, ':');
        size_t len = colon ? colon - e : ::std::strlen(e);

        if (len > 0) {
            DebugEnablePhase(::std::string(e, len).c_str());
        }

        if (colon) {
            e = colon + 1;
        } else {
            e = e + len;
        }
    }
}

void DebugSetPhase(const char* phaseName) {
    gsDebugPhase = phaseName;
    gbDebugPhaseEnabled = gmDisabledDebug.find(gsDebugPhase) == gmDisabledDebug.end();
    if (gbEnableHeaders) {
        ::std::cout << phaseName << ": BEGIN" << ::std::endl;
    }
}

bool DebugIsEnabled() {
    return gbDebugPhaseEnabled;
}

void DebugDisablePhase(const char* phaseName) {
    gmDisabledDebug.insert(::std::string(phaseName));
}

void DebugEnablePhase(const char* phaseName) {
    auto it = gmDisabledDebug.find(phaseName);
    if (it != gmDisabledDebug.end()) {
        gmDisabledDebug.erase(it);
    } else {
        ::std::cerr << "Unknown debug phase: " << phaseName << ::std::endl;
    }
}

void DebugPrint(::std::function<void(::std::ostream& os)> cb) {
    if (!DebugIsEnabled()) {
        return;
    }
    ::std::unique_lock<::std::mutex> _lh{gDebugLock};

    ::std::cout << gsDebugPhase << "- ";
    for (auto i = giIndentLevel; i--;) {
        ::std::cout << " ";
    }
    cb(::std::cout);
    ::std::cout << ::std::endl;
}

void DebugEnterScope(const char* name, dbgCbT cb) {
    if (!DebugIsEnabled()) {
        return;
    }
    ::std::unique_lock<::std::mutex> _lh{gDebugLock};

    ::std::cout << gsDebugPhase << "- ";
    for (auto i = giIndentLevel; i--;) {
        ::std::cout << " ";
    }
    ::std::cout << ">>> " << name << "(";
    cb(::std::cout);
    ::std::cout << ")" << ::std::endl;
    giIndentLevel++;
}

void DebugLeaveScope(const char* name, dbgCbT cb) {
    if (!DebugIsEnabled()) {
        return;
    }
    ::std::unique_lock<::std::mutex> _lh{gDebugLock};

    ::std::cout << gsDebugPhase << "- ";
    giIndentLevel--;
    for (auto i = giIndentLevel; i--;) {
        ::std::cout << " ";
    }
    ::std::cout << "<<< " << name << ::std::endl;
}

DebugFunctionScope::DebugFunctionScope(const char* name, dbgCbT cb)
    : name(name) {
    DebugEnterScope(name, cb);
}
DebugFunctionScope::~DebugFunctionScope() {
    DebugLeaveScope(name, [](auto&) {});
}
