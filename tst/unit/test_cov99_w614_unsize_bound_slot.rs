#![feature(unsize)]

use std::marker::Unsize;

fn length<S: ?Sized + Unsize<[u8]>>(source: &S) -> usize {
    let view = <&[_]>::from(source);
    view.len()
}

fn main() {
    assert_eq!(length(&[17u8, 29, 41]), 3);
}
