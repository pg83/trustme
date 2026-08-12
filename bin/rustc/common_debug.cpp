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

void DebugSetPhase(const char* phase_name) {
    gsDebugPhase = phase_name;
    gbDebugPhaseEnabled = gmDisabledDebug.find(gsDebugPhase) == gmDisabledDebug.end();
    if (gbEnableHeaders) {
        ::std::cout << phase_name << ": BEGIN" << ::std::endl;
    }
}

bool DebugIsEnabled() {
    return gbDebugPhaseEnabled;
}

void DebugDisablePhase(const char* phase_name) {
    gmDisabledDebug.insert(::std::string(phase_name));
}

void DebugEnablePhase(const char* phase_name) {
    auto it = gmDisabledDebug.find(phase_name);
    if (it != gmDisabledDebug.end()) {
        gmDisabledDebug.erase(it);
    } else {
        ::std::cerr << "Unknown debug phase: " << phase_name << ::std::endl;
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

void DebugEnterScope(const char* name, dbg_cb_t cb) {
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

void DebugLeaveScope(const char* name, dbg_cb_t cb) {
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

DebugFunctionScope::DebugFunctionScope(const char* name, dbg_cb_t cb)
    : m_name(name) {
    DebugEnterScope(m_name, cb);
}
DebugFunctionScope::~DebugFunctionScope() {
    DebugLeaveScope(m_name, [](auto&) {});
}
