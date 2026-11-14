//@ compile-flags: -Zdump-ast
//@ output-contains: _1_ast.rs new_unchecked
#![feature(pin_ergonomics)]

fn main() {
    let mut number = 11u32;
    let mut initial = core::pin::Pin::new(&mut number);
    let pinned = &pin mut *initial;
    *pinned.get_mut() = 29;
    assert_eq!(*initial, 29);
}
