#![feature(derive_coerce_pointee, pattern_types, pattern_type_macro)]

use std::marker::CoercePointee;

#[derive(CoercePointee)]
#[repr(transparent)]
struct Pointer<T: ?Sized>(*const T)
where
    std::pat::pattern_type!(*const T is !null): Sized;

fn main() {
    let values = [3u8, 5, 8];
    let source = Pointer(&values as *const [u8; 3]);
    let destination: Pointer<[u8]> = source;
    let slice = unsafe { &*destination.0 };
    assert_eq!(slice, &values);
    assert_eq!(slice.as_ptr(), values.as_ptr());
}
