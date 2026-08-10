#![feature(lang_items, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "add"]
pub trait Add<Rhs = Self> {
    type Output;

    fn add(self, rhs: Rhs) -> Self::Output;
}

impl Add for i32 {
    type Output = i32;

    fn add(self, _rhs: i32) -> i32 {
        self
    }
}

fn main() -> i32 {
    let _value: i32 = 1 + 2;
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
