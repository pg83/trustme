// Extracted from library/core/src/hint.rs:408
#![allow(unused)]
fn main() {
    use std::hint::black_box;

    let zero = 0;
    let five = 5;

    // The compiler will see this and remove the `* five` call, because it knows that multiplying
    // any integer by 0 will result in 0.
    let c = zero * five;

    // Adding `black_box` here disables the compiler's ability to reason about the first operand in the multiplication.
    // It is forced to assume that it can be any possible number, so it cannot remove the `* five`
    // operation.
    let c = black_box(zero) * five;
}
