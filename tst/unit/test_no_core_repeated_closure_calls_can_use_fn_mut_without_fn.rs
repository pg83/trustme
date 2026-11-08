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
#[lang = "fn_mut"]
pub trait FnMut<Args: Tuple>: FnOnce<Args> {
    extern "rust-call" fn call_mut(&mut self, args: Args) -> Self::Output;
}
fn repeat<F: FnMut() -> i32>(mut callback: F) -> i32 {
    let _ = callback();
    callback()
}
#[no_mangle]
extern "C" fn main() -> i32 {
    let zero = 0i32;
    let mut read = || zero;
    let _ = read();
    let mut result = 11i32;
    let write = || { result = read(); result };
    let _ = repeat(write);
    result
}
