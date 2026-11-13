#![feature(coerce_unsized, unsize)]

use std::marker::Unsize;
use std::ops::CoerceUnsized;

struct Relay<T: ?Sized>(*const T);

impl<T: ?Sized + Unsize<U>, U: ?Sized> CoerceUnsized<Relay<U>> for Relay<T> {}

fn main() {
    let input = None::<Relay<_>>;
    let output: Option<Relay<str>> = input.map(|value| value);
    assert!(output.is_none());
}
