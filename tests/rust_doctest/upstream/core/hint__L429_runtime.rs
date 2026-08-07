// Extracted from library/core/src/hint.rs:429
#![allow(unused)]
fn main() {
    use std::hint::black_box;

    // This is a simple function that increments its input by 1. Note that it is pure, meaning it
    // has no side-effects. This function has no effect if its result is unused. (An example of a
    // function *with* side-effects is `println!()`.)
    fn increment(x: u8) -> u8 {
        x + 1
    }

    // Here, we call `increment` but discard its result. The compiler, seeing this and knowing that
    // `increment` is pure, will eliminate this function call entirely. This may not be desired,
    // though, especially if we're trying to track how much time `increment` takes to execute.
    let _ = increment(black_box(5));

    // Here, we force `increment` to be executed. This is because the compiler treats `black_box`
    // as if it has side-effects, and thus must compute its input.
    let _ = black_box(increment(black_box(5)));
}
