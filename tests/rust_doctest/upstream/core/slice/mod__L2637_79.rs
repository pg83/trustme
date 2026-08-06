// Extracted from library/core/src/slice/mod.rs:2637
#![allow(unused)]
fn main() {
    let v = &[10, 40, 30];
    assert!(v.ends_with(&[]));
    let v: &[u8] = &[];
    assert!(v.ends_with(&[]));
}
