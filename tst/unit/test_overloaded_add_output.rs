#![feature(lang_items, no_core)]
#![no_core]
#![no_main]
//@ compile-flags: -Clink-arg=-lc

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

struct Left;
struct Right;

impl Add<Right> for Left {
    type Output = i64;

    fn add(self, _rhs: Right) -> i64 {
        unsafe {
            ADD_CALLED = 0;
        }
        3
    }
}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    let _value: i64 = Left + Right;

    unsafe { ADD_CALLED }
}
