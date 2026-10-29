//@ compile-fail: cannot transmute between types of different sizes
#![feature(extern_types)]
unsafe extern "C" { type Opaque; }
#[no_mangle]
pub unsafe fn convert(value: *const [u8]) -> *const Opaque {
    std::mem::transmute(value)
}
fn main() {}
