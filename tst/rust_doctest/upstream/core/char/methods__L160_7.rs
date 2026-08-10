// Extracted from library/core/src/char/methods.rs:160
#![allow(unused)]
fn main() {
    let c = '💯';
    let i = c as u32;

    assert_eq!(128175, i);
}
