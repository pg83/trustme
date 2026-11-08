#![feature(derive_coerce_pointee, pattern_types, pattern_type_macro)]

use std::marker::CoercePointee;

#[derive(CoercePointee)]
#[repr(transparent)]
struct Pointer<T: ?Sized>
where
    std::pat::pattern_type!(u16 is 1..=9): Copy,
{
    pointer: *const T,
}

fn main() {
    let values = [3u16, 7];
    let thin = Pointer { pointer: &values as *const [u16; 2] };
    let wide: Pointer<[u16]> = thin;
    std::assert_eq!(unsafe { &*wide.pointer }, &[3, 7]);
}
