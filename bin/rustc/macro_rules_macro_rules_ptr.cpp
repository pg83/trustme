#include "macro_rules_macro_rules_ptr.h"

#include "compile_error.h"
#include "macro_rules_macro_rules.h"

MacroRulesPtr::MacroRulesPtr()
    : ptr(nullptr)
{
}

MacroRulesPtr::MacroRulesPtr(MacroRules* p)
    : ptr(p)
{
}

MacroRulesPtr::~MacroRulesPtr() {
    if (ptr) {
        delete ptr;
        ptr = nullptr;
    }
}

MacroRulesPtr::MacroRulesPtr(MacroRulesPtr&& x)
    : ptr(x.ptr)
{
    x.ptr = nullptr;
}

MacroRulesPtr& MacroRulesPtr::operator=(MacroRulesPtr&& x) {
    ptr = x.ptr;
    x.ptr = nullptr;
    return *this;
}

const MacroRules& MacroRulesPtr::operator*() const {
    BUG_ASSERT(ptr);
    return *ptr;
}

MacroRules& MacroRulesPtr::operator*() {
    BUG_ASSERT(ptr);
    return *ptr;
}

const MacroRules* MacroRulesPtr::operator->() const {
    BUG_ASSERT(ptr);
    return ptr;
}

MacroRules* MacroRulesPtr::operator->() {
    BUG_ASSERT(ptr);
    return ptr;
}
