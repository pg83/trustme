//@ compile-fail: unterminated string escape
//@ compile-flags: --cfg 'label="tail\'
fn main() {}
