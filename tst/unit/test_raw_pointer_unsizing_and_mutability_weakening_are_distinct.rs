#![feature(coerce_unsized)]
use std::ops::CoerceUnsized;
fn require<S: CoerceUnsized<D>, D>() {}
fn main() {
    require::<*mut [u16; 3], *const [u16]>();
    require::<*mut [u16; 3], *mut [u16]>();
    require::<*const [u16; 3], *const [u16]>();
    let mut value = 37u16;
    let mutable = &mut value as *mut u16;
    let shared: *const u16 = mutable;
    assert_eq!(unsafe { *shared }, 37);
    assert_eq!(mutable as usize, shared as usize);
    let mut values = [2u16, 3, 5];
    let mutable = &mut values as *mut [u16; 3];
    let shared: *const [u16] = mutable;
    assert_eq!(unsafe { &*shared }, &[2, 3, 5]);
}
