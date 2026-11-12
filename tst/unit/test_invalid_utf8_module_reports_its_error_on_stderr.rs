//@ input: aux/invalid_utf8_module_lead.bin
//@ compile-fail: Invalid UTF-8 (too long)

#[path = "aux/invalid_utf8_module_lead.bin"]
mod invalid;

fn main() {}
