#![feature(pin_ergonomics)]
#![allow(incomplete_features)]

use std::pin::Pin;

struct Target;

impl Target {
    fn method(self: Pin<&Self>) -> usize {
        1
    }
}

fn call(value: Pin<&mut Target>) -> usize {
    value.method()
}

fn main() {
    let mut target = Target;
    assert_eq!(call(Pin::new(&mut target)), 1);
}
