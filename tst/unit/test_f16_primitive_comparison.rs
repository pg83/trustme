#![feature(f16, f128, lang_items, no_core)]
#![no_core]
#![no_main]
//@ compile-flags: -Clink-arg=-lc

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "legacy_receiver"]
pub trait LegacyReceiver: PointeeSized {}

impl<T: PointeeSized> LegacyReceiver for &T {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for f16 {}
impl Copy for f128 {}

#[lang = "eq"]
pub trait PartialEq<Rhs = Self> {
    fn eq(&self, other: &Rhs) -> bool;
}

#[lang = "partial_ord"]
pub trait PartialOrd<Rhs = Self>: PartialEq<Rhs> {
    fn partial_cmp(&self, other: &Rhs) -> bool;
    fn lt(&self, other: &Rhs) -> bool;
    fn le(&self, other: &Rhs) -> bool;
}

fn deferred<T>() -> T {
    loop {}
}

macro_rules! partial_eq_impl {
    ($($t:ty)*) => ($(
        impl PartialEq for $t {
            fn eq(&self, other: &Self) -> bool {
                *self == *other
            }
        }
    )*)
}

macro_rules! partial_ord_impl {
    ($($t:ty)*) => ($(
        impl PartialOrd for $t {
            fn partial_cmp(&self, _other: &Self) -> bool {
                *self <= unsafe { deferred() }
            }

            fn lt(&self, other: &Self) -> bool {
                *self < *other
            }

            fn le(&self, other: &Self) -> bool {
                *self <= *other
            }
        }
    )*)
}

partial_eq_impl! { f16 f128 }
partial_ord_impl! { f16 f128 }

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    0
}
