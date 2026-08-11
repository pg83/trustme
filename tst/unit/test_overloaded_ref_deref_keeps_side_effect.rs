#![feature(auto_traits, lang_items, no_core)]
#![no_core]
#![no_main]
//@ compile-flags: -Clink-arg=-lc

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "legacy_receiver"]
pub trait LegacyReceiver: PointeeSized {}

impl<T: PointeeSized> LegacyReceiver for &T {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for i32 {}
impl<T: PointeeSized> Copy for &T {}

#[lang = "freeze"]
pub unsafe auto trait Freeze {}

#[lang = "drop_in_place"]
pub unsafe fn drop_in_place<T: PointeeSized>(_to_drop: *mut T) {}

#[lang = "deref"]
pub trait Deref {
    type Target;

    fn deref(&self) -> &Self::Target;
}

static mut DEREF_CALLED: i32 = 1;

struct Wrapper(i32);

impl Deref for Wrapper {
    type Target = i32;

    fn deref(&self) -> &i32 {
        unsafe {
            DEREF_CALLED = 0;
        }
        &self.0
    }
}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    let wrapper = Wrapper(123);
    let _result: i32 = *wrapper;

    unsafe { DEREF_CALLED }
}
