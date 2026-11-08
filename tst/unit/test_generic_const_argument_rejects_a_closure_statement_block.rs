//@ compile-fail: overly complex generic constant
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Marker<const N: usize>;

type Deferred<T> = Marker<{
    let callback = &|| {
        let first = 19usize;
        let second = first + 4;
        second
    };
    let _ = callback;
    std::mem::size_of::<T>()
}>;

fn main() {}
