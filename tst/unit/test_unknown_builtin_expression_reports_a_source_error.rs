//@ compile-fail: unknown builtin expression
#![feature(builtin_syntax)]
fn main() { let _ = builtin # missing_operation(19); }
