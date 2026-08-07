// Extracted from library/core/src/iter/traits/iterator.rs:2951
#![allow(unused)]
#![feature(try_find)]
fn main() {

    use std::num::NonZero;

    let a = [3, 5, 7, 4, 9, 0, 11u32];
    let result = a.into_iter().try_find(|&x| NonZero::new(x).map(|y| y.is_power_of_two()));
    assert_eq!(result, Some(Some(4)));
    let result = a.into_iter().take(3).try_find(|&x| NonZero::new(x).map(|y| y.is_power_of_two()));
    assert_eq!(result, Some(None));
    let result = a.into_iter().rev().try_find(|&x| NonZero::new(x).map(|y| y.is_power_of_two()));
    assert_eq!(result, None);
}
