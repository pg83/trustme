// Extracted from library/core/src/macros/mod.rs:379
#![allow(unused)]
#![feature(assert_matches)]
fn main() {

    use std::assert_matches::debug_assert_matches;

    let a = Some(345);
    let b = Some(56);
    debug_assert_matches!(a, Some(_));
    debug_assert_matches!(b, Some(_));

    debug_assert_matches!(a, Some(345));
    debug_assert_matches!(a, Some(345) | None);

    // debug_assert_matches!(a, None); // panics
    // debug_assert_matches!(b, Some(345)); // panics
    // debug_assert_matches!(b, Some(345) | None); // panics

    debug_assert_matches!(a, Some(x) if x > 100);
    // debug_assert_matches!(a, Some(x) if x < 100); // panics
}
