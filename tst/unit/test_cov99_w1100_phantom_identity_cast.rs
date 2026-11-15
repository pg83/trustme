//@ compile-flags: -Z dump-mir -C panic=abort
use std::marker::PhantomData;

#[inline(never)]
fn cast_marker(value: PhantomData<u8>) -> PhantomData<u8> {
    value as PhantomData<u8>
}

#[inline(never)]
fn cast_scalar(value: u16) -> u8 {
    value as u8
}

fn main() {
    let marker = cast_marker(PhantomData);
    assert_eq!(std::mem::size_of_val(&marker), 0);
    assert_eq!(cast_scalar(257), 1);
}
