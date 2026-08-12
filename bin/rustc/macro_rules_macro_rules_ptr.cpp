#include "macro_rules_macro_rules_ptr.h"

MacroRulesPtr::MacroRulesPtr()
    : m_ptr(nullptr) {
}
MacroRulesPtr::MacroRulesPtr(MacroRulesPtr&& x)
    : m_ptr(x.m_ptr) {
    x.m_ptr = nullptr;
}
MacroRulesPtr& MacroRulesPtr::operator=(MacroRulesPtr&& x) {
    m_ptr = x.m_ptr;
    x.m_ptr = nullptr;
    return *this;
}
const MacroRules& MacroRulesPtr::operator*() const {
    assert(m_ptr);
    return *m_ptr;
}
MacroRules& MacroRulesPtr::operator*() {
    assert(m_ptr);
    return *m_ptr;
}
const MacroRules* MacroRulesPtr::operator->() const {
    assert(m_ptr);
    return m_ptr;
}
MacroRules* MacroRulesPtr::operator->() {
    assert(m_ptr);
    return m_ptr;
}
