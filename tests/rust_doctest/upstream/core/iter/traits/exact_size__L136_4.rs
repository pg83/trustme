// Extracted from library/core/src/iter/traits/exact_size.rs:136
#![allow(unused)]
#![feature(exact_size_is_empty)]
fn main() {

    let mut one_element = std::iter::once(0);
    assert!(!one_element.is_empty());

    assert_eq!(one_element.next(), Some(0));
    assert!(one_element.is_empty());

    assert_eq!(one_element.next(), None);
}
