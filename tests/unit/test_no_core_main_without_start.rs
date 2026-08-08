#![feature(lang_items, no_core)]
#![no_core]

fn main() -> i32 {
    0
}

#[panic_handler]
fn panic(_payload: usize) -> u32 {
    1
}
