//@ compile-fail: Undefined language item 'mrustc-panic_implementation' required
#![feature(start)]
#![no_std]

fn main() -> i32 {
    0
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
