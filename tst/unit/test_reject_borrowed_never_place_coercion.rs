//@ compile-fail: Type mismatch
#![feature(never_type)]
fn invalid(value: &!) -> &u8 { &*value }
fn main() {}
