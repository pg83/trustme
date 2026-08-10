// Extracted from src/ffi.md:537
#![allow(unused)]
fn main() {
    unsafe fn kaboom(ptr: *const i32) -> i32 { *ptr }
}
