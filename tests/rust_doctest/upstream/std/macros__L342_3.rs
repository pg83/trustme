// Extracted from library/std/src/macros.rs:342
#![allow(unused)]
fn main() {
    assert_eq!(1, dbg!(1u32,)); // trailing comma ignored
    assert_eq!((1,), dbg!((1u32,))); // 1-tuple
}
