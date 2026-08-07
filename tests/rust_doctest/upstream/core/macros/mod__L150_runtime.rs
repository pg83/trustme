// Extracted from library/core/src/macros/mod.rs:150
#![allow(unused)]
#![feature(assert_matches)]
fn main() {

    use std::assert_matches::assert_matches;

    let a = Some(345);
    let b = Some(56);
    assert_matches!(a, Some(_));
    assert_matches!(b, Some(_));

    assert_matches!(a, Some(345));
    assert_matches!(a, Some(345) | None);

    // assert_matches!(a, None); // panics
    // assert_matches!(b, Some(345)); // panics
    // assert_matches!(b, Some(345) | None); // panics

    assert_matches!(a, Some(x) if x > 100);
    // assert_matches!(a, Some(x) if x < 100); // panics
}
