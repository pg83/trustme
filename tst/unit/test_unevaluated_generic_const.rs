#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Deferred<T>([(); deferred::<T>()])
where
    [(); deferred::<T>()]:;

const fn deferred<T>() -> usize {
    panic!()
}

fn main() {}
