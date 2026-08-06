// Extracted from src/expressions/block-expr.md:136
#![allow(unused)]
#![ feature(never_type) ]
fn main() {
    fn make<T>() -> T { loop {} }
    struct Foo {
        x: !,
    }
    fn diverging_place_not_read() -> ! {
        let foo = Foo { x: make() };
        // Assignment to `_` means the place is not read.
        let _ = foo.x;
    } // ERROR: Mismatched types.
}
