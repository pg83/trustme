// Extracted from library/core/src/slice/mod.rs:4382
#![allow(unused)]
fn main() {
    let v = [1, 2, 3, 3, 5, 6, 7];
    let i = v.partition_point(|&x| x < 5);

    assert_eq!(i, 4);
    assert!(v[..i].iter().all(|&x| x < 5));
    assert!(v[i..].iter().all(|&x| !(x < 5)));
}
