// Extracted from library/core/src/iter/sources/from_coroutine.rs:14
#![allow(unused)]
#![feature(coroutines)]
#![feature(iter_from_coroutine)]
fn main() {
    
    let it = std::iter::from_coroutine(#[coroutine] || {
        yield 1;
        yield 2;
        yield 3;
    });
    let v: Vec<_> = it.collect();
    assert_eq!(v, [1, 2, 3]);
}
