// Extracted from library/core/src/slice/mod.rs:2483
#![allow(unused)]
fn main() {
    let mut s = [10, 40, 30, 20, 60, 50];

    for group in s.rsplitn_mut(2, |num| *num % 3 == 0) {
        group[0] = 1;
    }
    assert_eq!(s, [1, 40, 30, 20, 60, 1]);
}
