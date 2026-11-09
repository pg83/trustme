//@ compile-fail: yield
#![feature(coroutines)]

fn main() {
    yield 137u8;
}
