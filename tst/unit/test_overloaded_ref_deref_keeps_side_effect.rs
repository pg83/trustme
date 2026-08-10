#![feature(lang_items, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "deref"]
pub trait Deref {
    type Target;

    fn deref(&self) -> &Self::Target;
}

static mut DEREF_CALLED: i32 = 1;

impl Deref for &i32 {
    type Target = i32;

    fn deref(&self) -> &i32 {
        unsafe {
            DEREF_CALLED = 0;
        }
        *self
    }
}

fn main() -> i32 {
    let value = 123;
    let reference = &value;
    let _result: i32 = *reference;

    unsafe { DEREF_CALLED }
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
