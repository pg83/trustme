#include "macro_rules_macro_rules_ptr.h"

MacroRulesPtr::MacroRulesPtr()
    : ptr(nullptr) {
}
MacroRulesPtr::MacroRulesPtr(MacroRulesPtr&& x)
    : ptr(x.ptr) {
    x.ptr = nullptr;
}
MacroRulesPtr& MacroRulesPtr::operator=(MacroRulesPtr&& x) {
    ptr = x.ptr;
    x.ptr = nullptr;
    return *this;
}
const MacroRules& MacroRulesPtr::operator*() const {
    assert(ptr);
    return *ptr;
}
MacroRules& MacroRulesPtr::operator*() {
    assert(ptr);
    return *ptr;
}
const MacroRules* MacroRulesPtr::operator->() const {
    assert(ptr);
    return ptr;
}
MacroRules* MacroRulesPtr::operator->() {
    assert(ptr);
    return ptr;
}
