//@ proc-macro-aux-build: inspect_type_expansion_order.rs
#![feature(derive_coerce_pointee, pattern_types, pattern_type_macro)]

use std::marker::CoercePointee;

macro_rules! install_observer {
    () => { use inspect_type_expansion_order::InspectTypes; };
}

#[derive(CoercePointee, InspectTypes)]
#[repr(transparent)]
struct Handle<T: ?Sized>
where
    std::pat::pattern_type!(*const T is !null): Copy,
{
    pointer: *const T,
}

install_observer!();

fn main() {
    let values = [19u16, 23, 29];
    let source = Handle { pointer: &values as *const [u16; 3] };
    let target: Handle<[u16]> = source;
    assert_eq!(unsafe { &*target.pointer }, &[19, 23, 29]);
    assert!(OBSERVED_PATTERN);
}
