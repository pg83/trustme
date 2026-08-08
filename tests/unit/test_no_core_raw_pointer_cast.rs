#![feature(lang_items, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

fn main() -> i32 {
    let text = "x\0";
    let string_ptr = text as *const str;
    let _byte_ptr = string_ptr as *const i8;
    0
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}

#[panic_handler]
fn panic(_payload: usize) -> u32 {
    1
}
