//@ crate-type: lib
//@ compile-flags: --target=$DIR/inputs/powerpc_64le_layout.toml -Cemit-cpp-only
//@ input: inputs/powerpc_64le_layout.toml
//@ output-contains: .cpp u8 _padding1[4];
//@ output-contains: .cpp u8 _padding3[4];
#![feature(no_core, lang_items)]
#![no_core]

#[lang = "pointee_sized"]
pub trait PointeeSized {}
#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}
#[lang = "sized"]
pub trait Sized: MetaSized {}

#[repr(C)]
pub struct Frame {
    pub header: [u8; 3],
    pub payload: u64,
    pub sequence: u32,
    pub timestamp: f64,
}

#[no_mangle]
pub fn payload(frame: &Frame) -> &u64 {
    &frame.payload
}
