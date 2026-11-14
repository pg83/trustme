//@ crate-type: lib
//@ compile-flags: --emit=metadata
//@ compile-fail: type annotations needed
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}
#[lang = "copy"]
pub trait Copy {}
#[lang = "legacy_receiver"]
pub trait LegacyReceiver: PointeeSized {}
impl<T: ?Sized> LegacyReceiver for &T {}
#[lang = "clone"]
pub trait Clone: Sized {
    fn clone(&self) -> Self;
}

impl<T> Clone for (T,) {
    fn clone(&self) -> Self { loop {} }
}

fn make<T>() -> T { loop {} }

pub fn check() {
    let tuple = (make(),);
    let _copied = tuple.clone();
}
