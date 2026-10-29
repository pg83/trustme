//@ compile-fail: invalid hexadecimal string escape
//@ compile-flags: --cfg 'label="\xG1"'
fn main() {}
