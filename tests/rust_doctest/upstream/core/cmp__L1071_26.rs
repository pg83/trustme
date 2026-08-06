// Extracted from library/core/src/cmp.rs:1071
#![allow(unused)]
fn main() {
    assert_eq!((-3).clamp(-2, 1), -2);
    assert_eq!(0.clamp(-2, 1), 0);
    assert_eq!(2.clamp(-2, 1), 1);
}
