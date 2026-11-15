#![feature(unsize)]

use std::marker::Unsize;

trait Destination {
    type Target: ?Sized;
}

struct Slice;

impl Destination for Slice {
    type Target = [u8];
}

fn coerce<D: Destination, S: ?Sized>(value: &S) -> &D::Target
where
    S: Unsize<D::Target>,
{
    value
}

fn main() {
    let input = [3_u8, 5, 8];
    let output = coerce::<Slice, _>(&input);
    assert!(output.len() == 3);
    assert!(output[1] == 5);
}
