//@ compile-variant: -O
#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
static DATA: [u16; 3] = [17, 23, 29];
#[inline(never)]
const fn borrowed<Q>() -> Q where &'static [u16; 3]: CoerceUnsized<Q> { &DATA }
const SLICE: &[u16] = borrowed();
fn main() {
    assert_eq!(SLICE, &[17, 23, 29]);
    let runtime: &[u16] = borrowed();
    assert_eq!(runtime, SLICE);
}
