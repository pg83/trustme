#pragma once

class MacroRules;

class MacroRulesPtr {
    MacroRules* ptr;

public:
    MacroRulesPtr();

    MacroRulesPtr(MacroRules* p);

    MacroRulesPtr(MacroRulesPtr&& x);

    MacroRulesPtr& operator=(MacroRulesPtr&& x);

    ~MacroRulesPtr();

    operator bool() const {
        return ptr != nullptr;
    }

    const MacroRules& operator*() const;

    MacroRules& operator*();

    const MacroRules* operator->() const;

    MacroRules* operator->();
};
