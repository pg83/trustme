#![feature(lang_items, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "add"]
pub trait Add<Rhs = Self> {
    type Output;

    fn add(self, rhs: Rhs) -> Self::Output;
}

static mut ADD_CALLED: i32 = 1;

impl Add for i32 {
    type Output = i32;

    fn add(self, rhs: i32) -> i32 {
        unsafe {
            ADD_CALLED = 0;
        }
        self + rhs
    }
}

fn main() -> i32 {
    let value;
    value = 1 + 2;

    unsafe { ADD_CALLED }
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
