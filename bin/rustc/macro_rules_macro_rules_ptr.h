#pragma once

#include <cassert>

class MacroRules;

class MacroRulesPtr {
    MacroRules* m_ptr;

public:
    MacroRulesPtr();

    MacroRulesPtr(MacroRules* p);

    MacroRulesPtr(MacroRulesPtr&& x);

    MacroRulesPtr& operator=(MacroRulesPtr&& x);

    ~MacroRulesPtr();

    operator bool() const {
        return m_ptr != nullptr;
    }

    const MacroRules& operator*() const;

    MacroRules& operator*();

    const MacroRules* operator->() const;

    MacroRules* operator->();
};
