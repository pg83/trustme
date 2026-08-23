#![feature(lang_items, no_core)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for i32 {}

#[lang = "drop"]
pub trait Drop {
    fn drop(&mut self);
}

#[lang = "destruct"]
pub trait Destruct {}

impl Destruct for i32 {}

#[lang = "drop_in_place"]
pub unsafe fn drop_in_place<T: PointeeSized>(_value: *mut T) {}

#[lang = "panic_in_cleanup"]
fn panic_in_cleanup() -> ! {
    loop {}
}

unsafe extern "C-unwind" {
    fn trustme_may_unwind(value: i32) -> i32;
}

struct Guard(i32);

enum GenericChoice<T> {
    Value(T),
    Guard(Guard),
}

impl Drop for Guard {
    fn drop(&mut self) {
        unsafe {
            trustme_may_unwind(self.0);
        }
    }
}

#[no_mangle]
pub fn trustme_noop_cleanup_probe(value: i32) -> i32 {
    unsafe { trustme_may_unwind(value) }
}

#[inline(never)]
fn generic_noop_cleanup<T: Destruct>(value: T, argument: i32) -> i32 {
    let _value = value;
    unsafe { trustme_may_unwind(argument) }
}

#[inline(never)]
fn generic_projected_noop_drop<T: Destruct>(choice: GenericChoice<T>, argument: i32) -> i32 {
    let _guard = Guard(argument);
    match choice {
        GenericChoice::Value(_value) => unsafe { trustme_may_unwind(argument) },
        GenericChoice::Guard(_inner) => argument,
    }
}

#[no_mangle]
pub fn trustme_monomorphized_noop_cleanup_probe(value: i32) -> i32 {
    generic_noop_cleanup(value, value)
        + generic_projected_noop_drop(GenericChoice::Value(value), value)
}

#[no_mangle]
pub fn trustme_real_cleanup_probe(value: i32) -> i32 {
    let _guard = Guard(value);
    unsafe { trustme_may_unwind(value) }
}
