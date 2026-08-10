// Extracted from library/core/src/iter/sources/generator.rs:11
#![allow(unused)]
#![feature(iter_macro, coroutines)]
fn main() {

    let it = std::iter::iter!{|| {
        yield 1;
        yield 2;
        yield 3;
    } }();
    let v: Vec<_> = it.collect();
    assert_eq!(v, [1, 2, 3]);
}
