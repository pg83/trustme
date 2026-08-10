// Extracted from library/core/src/bool.rs:21
#![allow(unused)]
fn main() {
    let mut a = 0;
    let mut function_with_side_effects = || { a += 1; };

    true.then_some(function_with_side_effects());
    false.then_some(function_with_side_effects());

    // `a` is incremented twice because the value passed to `then_some` is
    // evaluated eagerly.
    assert_eq!(a, 2);
}
