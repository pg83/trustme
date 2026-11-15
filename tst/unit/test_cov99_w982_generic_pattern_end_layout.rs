//@ crate-type: lib
#![feature(pattern_types, generic_pattern_types, core_intrinsics, rustc_attrs)]
#![allow(incomplete_features, internal_features)]

#[rustc_builtin_macro]
macro_rules! pattern_type {
    ($($tokens:tt)*) => {};
}

pub fn range_end_layout<const END: u8>() -> usize {
    core::intrinsics::size_of::<(pattern_type!(u8 is ..=END),)>()
}
