#![feature(f16, lang_items, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

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

extern "Rust" {
    fn deferred<T>() -> T;
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

fn main() -> i32 {
    0
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
