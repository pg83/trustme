/*
 * Debugging helpers
 */
#include "common_debug.h"
#include <iostream>
#include <cstring> // strr

void CommonDebugContext::processEnable(const char* e) {
    if (*e) {
        enableHeaders_ = true;
    }
    while (*e) {
        const char* colon = ::std::strchr(e, ':');
        size_t len = colon ? colon - e : ::std::strlen(e);

        if (len > 0) {
            enablePhase(::std::string(e, len).c_str());
        }

        if (colon) {
            e = colon + 1;
        } else {
            e = e + len;
        }
    }
}

void CommonDebugContext::setPhase(const char* phaseName) {
    phase_ = phaseName;
    phaseEnabled_ = disabledPhases_.find(phase_) == disabledPhases_.end();
    if (enableHeaders_) {
        ::std::cout << phaseName << ": BEGIN" << ::std::endl;
    }
}

bool CommonDebugContext::isEnabled() const {
    return phaseEnabled_;
}

void CommonDebugContext::disablePhase(const char* phaseName) {
    disabledPhases_.insert(::std::string(phaseName));
}

void CommonDebugContext::enablePhase(const char* phaseName) {
    auto it = disabledPhases_.find(phaseName);
    if (it != disabledPhases_.end()) {
        disabledPhases_.erase(it);
    } else {
        ::std::cerr << "Unknown debug phase: " << phaseName << ::std::endl;
    }
}

void CommonDebugContext::print(DebugStreamCallback& cb) {
    if (!isEnabled()) {
        return;
    }
    ::std::unique_lock<::std::mutex> lock{lock_};

    ::std::cout << phase_ << "- ";
    for (auto i = indentLevel_; i--;) {
        ::std::cout << " ";
    }
    cb.write(::std::cout);
    ::std::cout << ::std::endl;
}

void CommonDebugContext::enterScope(const char* name, DebugStreamCallback& cb) {
    if (!isEnabled()) {
        return;
    }
    ::std::unique_lock<::std::mutex> lock{lock_};

    ::std::cout << phase_ << "- ";
    for (auto i = indentLevel_; i--;) {
        ::std::cout << " ";
    }
    ::std::cout << ">>> " << name << "(";
    cb.write(::std::cout);
    ::std::cout << ")" << ::std::endl;
    indentLevel_++;
}

void CommonDebugContext::leaveScope(const char* name) {
    if (!isEnabled()) {
        return;
    }
    ::std::unique_lock<::std::mutex> lock{lock_};

    ::std::cout << phase_ << "- ";
    indentLevel_--;
    for (auto i = indentLevel_; i--;) {
        ::std::cout << " ";
    }
    ::std::cout << "<<< " << name << ::std::endl;
}

DebugFunctionScope::~DebugFunctionScope() {
    context.leaveScope(name);
}
