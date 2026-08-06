// Extracted from library/core/src/slice/mod.rs:4394
#![allow(unused)]
fn main() {
    let a = [2, 4, 8];
    assert_eq!(a.partition_point(|x| x < &100), a.len());
    let a: [i32; 0] = [];
    assert_eq!(a.partition_point(|x| x < &100), 0);
}
