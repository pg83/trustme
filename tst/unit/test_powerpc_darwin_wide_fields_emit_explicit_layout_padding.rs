//@ crate-type: lib
//@ compile-flags: --target=powerpc-apple-darwin -Cemit-cpp-only
//@ output-contains: .cpp u8 _padding1[4];
//@ output-contains: .cpp u8 _padding2[4];
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

#[repr(C)]
pub struct WideFirst {
    pub word: u64,
    pub tag: u8,
    pub tail: f64,
}

#[repr(C)]
pub struct ByteFirst {
    pub tag: u8,
    pub word: u64,
    pub tail: f64,
}

#[no_mangle]
pub fn first_word(value: &WideFirst) -> &u64 {
    &value.word
}

#[no_mangle]
pub fn later_word(value: &ByteFirst) -> &u64 {
    &value.word
}
