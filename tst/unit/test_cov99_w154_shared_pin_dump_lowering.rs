//@ compile-flags: -Zdump-ast
//@ output-contains: _1_ast.rs new_unchecked
#![feature(pin_ergonomics)]
#![allow(incomplete_features)]

fn main() {
    let value = 31u32;
    let pinned = &pin const value;
    assert_eq!(*pinned, 31);
}
