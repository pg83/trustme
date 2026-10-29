//@ compile-fail: incomplete hexadecimal string escape
//@ compile-flags: --cfg 'label="\x4'
fn main() {}
