//@ compile-variant: -O
#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
#[inline(never)]
const fn widen<P: CoerceUnsized<Q>, Q>(pointer: P) -> Q { pointer }
static DATA: [u16; 3] = [17, 23, 29];
static mut MUTABLE: [u16; 3] = [31, 37, 41];
const SHARED: *const [u16] = widen(&DATA);
const FROM_MUTABLE: *const [u16] = widen(&raw mut MUTABLE);
fn main() {
    assert_eq!(unsafe { &*SHARED }, &[17, 23, 29]);
    assert_eq!(unsafe { &*FROM_MUTABLE }, &[31, 37, 41]);
    let mut values = std::hint::black_box([43u16, 47, 53]);
    let pointer: *const [u16] = widen(&mut values as *mut [u16; 3]);
    assert_eq!(unsafe { &*pointer }, &[43, 47, 53]);
}
