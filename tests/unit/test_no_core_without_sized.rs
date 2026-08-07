#![feature(lang_items, no_core, start)]
#![no_core]

fn main() -> i32 {
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
