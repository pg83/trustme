// Extracted from library/core/src/slice/mod.rs:2310
#![allow(unused)]
fn main() {
    let mut v = [10, 40, 30, 20, 60, 50];
    
    for group in v.split_inclusive_mut(|num| *num % 3 == 0) {
        let terminator_idx = group.len()-1;
        group[terminator_idx] = 1;
    }
    assert_eq!(v, [10, 40, 1, 20, 1, 1]);
}
