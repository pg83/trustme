// Extracted from library/core/src/slice/mod.rs:3085
#![allow(unused)]
fn main() {
    let mut v = [4, -5, 1, -3, 2];
    
    v.sort_unstable();
    assert_eq!(v, [-5, -3, 1, 2, 4]);
}
