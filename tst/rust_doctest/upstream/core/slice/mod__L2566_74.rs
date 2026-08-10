// Extracted from library/core/src/slice/mod.rs:2566
#![allow(unused)]
fn main() {
    let v = [10, 40, 30];
    assert!(v.contains(&30));
    assert!(!v.contains(&50));
}
