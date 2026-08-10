#![feature(lang_items, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "eq"]
pub trait PartialEq<Rhs = Self> {
    fn eq(&self, other: &Rhs) -> bool;
}

impl PartialEq for *const u8 {
    fn eq(&self, other: &*const u8) -> bool {
        *self == *other
    }
}

fn main() -> i32 {
    0
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
