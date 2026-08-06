// Extracted from library/core/src/slice/mod.rs:2251
#![allow(unused)]
fn main() {
    let mut v = [10, 40, 30, 20, 60, 50];
    
    for group in v.split_mut(|num| *num % 3 == 0) {
        group[0] = 1;
    }
    assert_eq!(v, [1, 40, 30, 1, 60, 1]);
}
