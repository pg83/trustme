//@ crate-type: lib
//@ compile-flags: -Cemit-cpp-only
#![feature(no_core, lang_items, unboxed_closures)]
#![no_core]

#[lang = "pointee_sized"] pub trait PointeeSized {}
#[lang = "meta_sized"] pub trait MetaSized: PointeeSized {}
#[lang = "sized"] pub trait Sized: MetaSized {}
#[lang = "legacy_receiver"] pub trait LegacyReceiver: PointeeSized {}
impl<T: PointeeSized> LegacyReceiver for &T {}
impl<T: PointeeSized> LegacyReceiver for &mut T {}
#[lang = "drop"] pub trait Drop { fn drop(&mut self); }
#[lang = "drop_in_place"] pub unsafe fn drop_in_place<T: PointeeSized>(_: *mut T) {}
#[lang = "destruct"] pub trait Destruct {}
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
#[lang = "fn"]
pub trait Fn<Args: Tuple>: FnMut<Args> {
    extern "rust-call" fn call(&self, args: Args) -> Self::Output;
}

pub fn selected() -> [u8; {
    let _empty = || 109u8;
    0
}] {
    []
}
