//@ compile-flags: -O

use std::ops::Deref;

#[inline(always)]
fn read_and_release<P: Deref<Target = u16>>(pointer: P) -> u16 {
    let value = *pointer;
    drop(pointer);
    value
}

#[inline(never)]
fn observe(value: &u16) -> u16 {
    read_and_release(&*value)
}

fn main() {
    let first = std::hint::black_box(443u16);
    let second = std::hint::black_box(449u16);
    assert_eq!(observe(&first), 443);
    assert_eq!(observe(&second), 449);
}
