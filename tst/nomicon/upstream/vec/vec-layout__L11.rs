// Extracted from src/vec/vec-layout.md:11
#![allow(unused)]
fn main() {
    pub struct Vec<T> {
        ptr: *mut T,
        cap: usize,
        len: usize,
    }
}
