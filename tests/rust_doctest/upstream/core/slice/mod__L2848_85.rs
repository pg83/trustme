// Extracted from library/core/src/slice/mod.rs:2848
#![allow(unused)]
fn main() {
    let s = [0, 1, 1, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55];

    let low = s.partition_point(|x| x < &1);
    assert_eq!(low, 1);
    let high = s.partition_point(|x| x <= &1);
    assert_eq!(high, 5);
    let r = s.binary_search(&1);
    assert!((low..high).contains(&r.unwrap()));

    assert!(s[..low].iter().all(|&x| x < 1));
    assert!(s[low..high].iter().all(|&x| x == 1));
    assert!(s[high..].iter().all(|&x| x > 1));

    // For something not found, the "range" of equal items is empty
    assert_eq!(s.partition_point(|x| x < &11), 9);
    assert_eq!(s.partition_point(|x| x <= &11), 9);
    assert_eq!(s.binary_search(&11), Err(9));
}
