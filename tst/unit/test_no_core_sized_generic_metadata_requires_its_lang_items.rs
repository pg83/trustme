//@ compile-fail: error
#![feature(no_core)]
#![no_core]

struct Tail<T>(T);
impl<T> Tail<T> {
    fn address<U>(&self, other: &Tail<U>) -> *const U { &other.0 }
}

fn main() {}
