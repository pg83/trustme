#![feature(lang_items, never_type, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "eq"]
pub trait PartialEq<Rhs = Self> {
    fn eq(&self, other: &Rhs) -> bool;
}

impl PartialEq for ! {
    fn eq(&self, _other: &!) -> bool {
        *self
    }
}

fn main() -> i32 {
    0
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
