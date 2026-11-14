//@ compile-only
//@ compile-flags: --emit=metadata -Z dump-hir -Z dump-mir
#![feature(pattern_types, pattern_type_macro, generic_pattern_types, generic_const_exprs)]
#![allow(incomplete_features, internal_features)]

use std::marker::PhantomData;
type Through<const END: usize> = std::pat::pattern_type!(usize is ..=END);

fn carry<T>(_: &T) -> PhantomData<Through<{ std::mem::size_of::<T>() }>>
where
    [(); std::mem::size_of::<T>()]:,
{
    PhantomData
}

fn main() {
    let bytes = [9u8; 3];
    let callback = move || bytes[1];
    let _ = carry(&callback);
}
