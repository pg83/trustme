//@ compile-fail: Undefined language item 'index' required
#![feature(lang_items, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

fn main() -> i32 {
    let values = [0i32, 1];
    values[0]
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
