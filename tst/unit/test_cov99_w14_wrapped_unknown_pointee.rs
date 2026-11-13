#![feature(coerce_unsized)]

use std::ops::CoerceUnsized;

struct Relay<T>(T);

impl<T, U> CoerceUnsized<Relay<U>> for Relay<T> where T: CoerceUnsized<U> {}

fn main() {
    let input = None::<Relay<&_>>;
    let output: Option<Relay<&str>> = input.map(|value| value);
    assert!(output.is_none());
}
