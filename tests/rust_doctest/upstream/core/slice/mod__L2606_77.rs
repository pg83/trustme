// Extracted from library/core/src/slice/mod.rs:2606
#![allow(unused)]
fn main() {
    let v = &[10, 40, 30];
    assert!(v.starts_with(&[]));
    let v: &[u8] = &[];
    assert!(v.starts_with(&[]));
}
