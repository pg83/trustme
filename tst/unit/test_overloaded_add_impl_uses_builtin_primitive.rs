#![feature(lang_items, no_core)]
#![no_core]
#![no_main]
//@ compile-flags: -Coverflow-checks=off -Clink-arg=-lc

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for i32 {}

#[lang = "drop_in_place"]
pub unsafe fn drop_in_place<T: PointeeSized>(_to_drop: *mut T) {}

#[lang = "add"]
pub trait Add<Rhs = Self> {
    type Output;

    fn add(self, rhs: Rhs) -> Self::Output;
}

static mut ADD_CALLED: i32 = 1;

struct Number(i32);

impl Add for i32 {
    type Output = i32;

    fn add(self, rhs: i32) -> i32 {
        self + rhs
    }
}

impl Add for Number {
    type Output = Number;

    fn add(self, rhs: Number) -> Number {
        unsafe {
            ADD_CALLED = 0;
        }
        Number(self.0 + rhs.0)
    }
}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    let value = Number(1) + Number(2);
    match value.0 {
        3 => unsafe { ADD_CALLED },
        _ => 1,
    }
}
