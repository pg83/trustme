//@ crate-type: lib
//@ compile-flags: --target=powerpc-apple-darwin -Cemit-cpp-only
//@ compile-fail: only 64-bit little-endian targets are supported
#![feature(no_core, lang_items)]
#![no_core]
#![allow(internal_features)]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

#[repr(C, packed(4))]
pub struct Payload {
    pub tag: u8,
    pub wide: u64,
    pub tail: u32,
}

#[no_mangle]
pub extern "C" fn echo_payload(value: Payload) -> Payload { value }
