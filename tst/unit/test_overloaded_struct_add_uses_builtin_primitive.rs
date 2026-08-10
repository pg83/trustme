#![feature(lang_items, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

#[lang = "add"]
pub trait Add<Rhs = Self> {
    type Output;

    fn add(self, rhs: Rhs) -> Self::Output;
}

struct Foo(i32);

static mut FOO_ADD_CALLED: i32 = 1;

impl Add for Foo {
    type Output = Foo;

    fn add(self, other: Foo) -> Foo {
        let result = Foo(self.0 + other.0);
        unsafe {
            FOO_ADD_CALLED = 0;
        }
        result
    }
}

fn main() -> i32 {
    let _value = Foo(1) + Foo(2);

    unsafe { FOO_ADD_CALLED }
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
