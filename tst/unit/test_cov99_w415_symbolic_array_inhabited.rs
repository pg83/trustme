//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ env: TRUSTME_CAPS_ORACLE=1
#![feature(core_intrinsics, generic_const_exprs)]
#![allow(internal_features, incomplete_features)]

const fn inhabited<const N: usize>() -> usize {
    unsafe { core::intrinsics::assert_inhabited::<std::array::IntoIter<u8, N>>() };
    0
}

pub struct Probe<const N: usize, const M: usize = { inhabited::<N>() }>;
