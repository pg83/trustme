// Extracted from library/core/src/slice/mod.rs:893
#![allow(unused)]
fn main() {
    let mut v = ["a", "b", "c", "d", "e"];
    v.swap(2, 4);
    assert!(v == ["a", "b", "e", "d", "c"]);
}
