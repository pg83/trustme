//@ aux-build: delegation_shapes.rs
#![feature(fn_delegation)]
#![allow(incomplete_features)]

use delegation_shapes::{Inner, Shapes};

struct Wrapper(Inner);
impl Shapes for Wrapper {
    reuse Shapes::* { self.0 }
}

fn double(value: u8) -> u8 { value * 2 }

fn main() {
    let wrapper = Wrapper(Inner);
    assert_eq!(wrapper.unsigned((1, 2, 3, 4, 1 << 100, 6)), (1, 2, 3, 4, 1 << 100, 6));
    assert_eq!(wrapper.signed((-1, -2, -3, -4, -(1 << 100), -6)), (-1, -2, -3, -4, -(1 << 100), -6));
    assert_eq!(wrapper.scalars((1.5, -2.25, true, 'λ')), (1.5, -2.25, true, 'λ'));
    assert_eq!(wrapper.bytes(&[2, 4, 6]), 3);
    assert_eq!(wrapper.text("λ!"), 3);
    let mut value = 0;
    wrapper.change(&mut value, 7);
    assert_eq!(value, 7);
    wrapper.pointers(&3, &mut value, double);
    assert_eq!(value, 6);
    assert_eq!(wrapper.object(&9u8), 9);
}
