//@ crate-type: lib
//@ compile-flags: -Z dump-mir -Z mir-opt-level=0 -C panic=abort
#![feature(lang_items, no_core, pattern_types, rustc_attrs)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "copy"]
pub trait Copy {}

#[lang = "phantom_data"]
#[repr(transparent)]
pub struct Marker<T>(pub *const T);

#[rustc_builtin_macro]
macro_rules! pattern_type { ($($tokens:tt)*) => {} }

type NonNullMarker = pattern_type!(Marker<u8> is !null);

#[inline(never)]
fn keep(_value: &NonNullMarker, flag: bool) -> bool {
    flag
}

#[no_mangle]
pub fn guard_marker(input: NonNullMarker, flag: bool) -> NonNullMarker {
    match input {
        value if keep(&value, flag) => value,
        value => value,
    }
}
