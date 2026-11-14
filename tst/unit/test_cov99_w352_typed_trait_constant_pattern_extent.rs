//@ compile-only
//@ compile-flags: --emit=metadata -Z dump-hir -Z dump-mir
#![feature(pattern_types, pattern_type_macro, generic_pattern_types, generic_const_exprs)]
#![allow(incomplete_features, internal_features)]

use std::marker::PhantomData;
type Upper<const END: usize> = std::pat::pattern_type!(usize is ..=END);
struct Fixed<const N: usize>;

const fn tag<T>(_: &T) -> PhantomData<Upper<{ std::mem::size_of::<T>() }>>
where
    [(); std::mem::size_of::<T>()]:,
{
    PhantomData
}

trait HasExtent {
    const EXTENT: usize = {
        let bytes = [2u8; 3];
        let callback = move || bytes[0];
        let _ = tag(&callback);
        3
    };
}
impl HasExtent for () {}

fn produce<T: HasExtent>() -> Fixed<{ T::EXTENT }>
where
    [(); T::EXTENT]:,
{
    Fixed
}

fn main() {
    let _: Fixed<3> = produce::<()>();
}
