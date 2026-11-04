#![feature(auto_traits, cfg_version, lang_items, no_core)]
#![no_core]
//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only -Cpanic=unwind -Zmir-opt-level=3
//@ compile-variant: -O

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
#[lang = "freeze"]
pub unsafe auto trait Freeze {}
#[lang = "unpin"]
pub auto trait Unpin {}
#[cfg(version("1.96"))]
#[lang = "unsafe_unpin"]
pub unsafe auto trait UnsafeUnpin {}
#[lang = "copy"]
pub trait Copy {}
#[lang = "drop"]
pub trait Drop { fn drop(&mut self); }
#[lang = "destruct"]
pub trait Destruct {}
#[lang = "drop_in_place"]
pub unsafe fn drop_in_place<T: PointeeSized>(_value: *mut T) {}
#[lang = "panic_in_cleanup"]
fn panic_in_cleanup() -> ! { loop {} }

#[lang = "owned_box"]
pub struct Owner<T>(Unique<T>);
struct Unique<T>(NonNull<T>);
struct NonNull<T>(*mut T);

extern "Rust" {
    fn release();
    fn destroy();
}

impl<T> Drop for Owner<T> {
    fn drop(&mut self) { unsafe { release(); } }
}

pub struct Payload;
impl Drop for Payload {
    fn drop(&mut self) { unsafe { destroy(); } }
}

pub fn dispose(_owner: Owner<Payload>) {}
