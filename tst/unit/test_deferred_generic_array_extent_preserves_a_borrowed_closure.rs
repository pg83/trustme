//@ compile-fail: overly complex generic constant
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

type Deferred<T> = [u8; {
    let callback = &|| {
        let value = 17usize;
        value
    };
    let _ = callback;
    std::mem::size_of::<T>()
}];

fn main() {}
