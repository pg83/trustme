// Extracted from library/core/src/mem/mod.rs:967
#![allow(unused)]
fn main() {
    pub const fn copy<T: Copy>(x: &T) -> T { *x }
}
