// Extracted from library/core/src/iter/traits/iterator.rs:3405
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];

    let v_copied: Vec<_> = a.iter().copied().collect();

    // copied is the same as .map(|&x| x)
    let v_map: Vec<_> = a.iter().map(|&x| x).collect();

    assert_eq!(v_copied, [1, 2, 3]);
    assert_eq!(v_map, [1, 2, 3]);
}
