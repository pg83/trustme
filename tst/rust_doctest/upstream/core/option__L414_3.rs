// Extracted from library/core/src/option.rs:414
#![allow(unused)]
fn main() {
    let yep = Some(42);
    let nope = None;
    // chain() already calls into_iter(), so we don't have to do so
    let nums: Vec<i32> = (0..4).chain(yep).chain(4..8).collect();
    assert_eq!(nums, [0, 1, 2, 3, 42, 4, 5, 6, 7]);
    let nums: Vec<i32> = (0..4).chain(nope).chain(4..8).collect();
    assert_eq!(nums, [0, 1, 2, 3, 4, 5, 6, 7]);
}
