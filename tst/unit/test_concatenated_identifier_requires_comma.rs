//@ compile-fail: expected one of
#![feature(concat_idents)]
fn main() { let _ = concat_idents!(prefix_ body); }
