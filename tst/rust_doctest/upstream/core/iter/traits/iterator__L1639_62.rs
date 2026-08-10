// Extracted from library/core/src/iter/traits/iterator.rs:1639
#![allow(unused)]
#![feature(iter_map_windows)]
fn main() {

    let mut it = [0.5, 1.0, 3.5, 3.0, 8.5, 8.5, f32::NAN].iter()
        .map_windows(|[a, b]| a <= b);

    assert_eq!(it.next(), Some(true));  // 0.5 <= 1.0
    assert_eq!(it.next(), Some(true));  // 1.0 <= 3.5
    assert_eq!(it.next(), Some(false)); // 3.5 <= 3.0
    assert_eq!(it.next(), Some(true));  // 3.0 <= 8.5
    assert_eq!(it.next(), Some(true));  // 8.5 <= 8.5
    assert_eq!(it.next(), Some(false)); // 8.5 <= NAN
    assert_eq!(it.next(), None);
}
