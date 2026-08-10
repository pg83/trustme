#![feature(lang_items, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "deref"]
pub trait Deref {
    type Target;

    fn deref(&self) -> &Self::Target;
}

struct Smart<T>(*const T);

impl<T> Deref for Smart<T> {
    type Target = T;

    fn deref(&self) -> &T {
        &**self
    }
}

fn main() -> i32 {
    0
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
