//@ compile-fail: Couldn't find type name 'Absent'
fn takes(_: std::mem::Absent) {}
fn main() {}
