// Extracted from library/alloc/src/str.rs:523
#![allow(unused)]
extern crate alloc;
fn main() {
    // this will panic at runtime
    let huge = "0123456789abcdef".repeat(usize::MAX);
}
