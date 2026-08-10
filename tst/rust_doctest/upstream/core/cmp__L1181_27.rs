// Extracted from library/core/src/cmp.rs:1181
#![allow(unused)]
fn main() {
    let a = f64::sqrt(-1.0);
    assert_eq!(a <= a, false);
}
