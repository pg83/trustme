//@ compile-flags: -Cpanic=abort -Clink-arg=-lc
#![feature(no_core, lang_items, unboxed_closures)]
#![no_core]
#![no_main]

#[lang = "pointee_sized"] pub trait PointeeSized {}
#[lang = "meta_sized"] pub trait MetaSized: PointeeSized {}
#[lang = "sized"] pub trait Sized: MetaSized {}
#[lang = "legacy_receiver"] pub trait LegacyReceiver: PointeeSized {}
impl<T: PointeeSized> LegacyReceiver for &T {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}
#[lang = "copy"] pub trait Copy {}
impl Copy for i32 {}
#[lang = "destruct"] pub trait Destruct {}
#[lang = "drop_in_place"] pub unsafe fn drop_in_place<T: PointeeSized>(_: *mut T) {}
#[lang = "tuple_trait"] pub trait Tuple {}

#[lang = "fn_once"]
pub trait FnOnce<Args: Tuple> {
    #[lang = "fn_once_output"] type Output;
    extern "rust-call" fn call_once(self, args: Args) -> Self::Output;
}

fn shared_result() -> i32 {
    let zero = 0i32;
    let read = || zero;
    read()
}

#[no_mangle]
extern "C" fn main() -> i32 {
    let replacement = shared_result();
    let mut value = 11i32;
    let set = || { value = replacement; };
    set();
    value
}
