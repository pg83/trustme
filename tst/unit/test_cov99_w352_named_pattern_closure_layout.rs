//@ compile-only
//@ compile-flags: --emit=metadata -Z dump-hir -Z dump-mir
#![feature(pattern_types, pattern_type_macro, generic_pattern_types, generic_const_exprs)]
#![allow(incomplete_features, internal_features)]

use std::marker::PhantomData;
struct Limits<T>(PhantomData<T>);
impl<T> Limits<T> { const END: usize = std::mem::size_of::<T>(); }
type Upper<T> = std::pat::pattern_type!(usize is ..=Limits::<T>::END);
struct Fixed<const N: usize>;

const fn tag<T>(_: &T) -> PhantomData<Upper<T>> { PhantomData }

trait HasExtent {
    const EXTENT: usize = {
        let bytes = [6u8; 3];
        let callback = move || bytes[1];
        let _ = tag(&callback);
        3
    };
}
impl HasExtent for () {}

fn produce<T: HasExtent, const N: usize>() -> Fixed<{ N + T::EXTENT }>
where
    [(); N + T::EXTENT]:,
{
    Fixed
}

fn check<const N: usize>()
where
    [(); N + 3]:,
{
    let _: Fixed<{ N + 3 }> = produce::<(), N>();
}

fn main() { check::<2>(); }
