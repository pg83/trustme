// Extracted from library/alloc/src/slice.rs:499
#![allow(unused)]
extern crate alloc;
fn main() {
    // this will panic at runtime
    b"0123456789abcdef".repeat(usize::MAX);
}
