// Extracted from library/std/src/macros.rs:334
#![allow(unused)]
fn main() {
    assert_eq!(dbg!(1usize, 2u32), (1, 2));
}
