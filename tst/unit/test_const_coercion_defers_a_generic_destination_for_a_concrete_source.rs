//@ compile-variant: -O
#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
const fn widen<Q>(pointer: *const [u16; 3]) -> Q
where *const [u16; 3]: CoerceUnsized<Q> { pointer }
static DATA: [u16; 3] = [17, 23, 29];
const POINTER: *const [u16] = widen(&DATA);
fn main() { assert_eq!(unsafe { &*POINTER }, &[17, 23, 29]); }
