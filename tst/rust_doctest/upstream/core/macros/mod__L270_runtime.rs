// Extracted from library/core/src/macros/mod.rs:270
#![allow(unused)]
fn main() {
    // the panic message for these assertions is the stringified value of the
    // expression given.
    debug_assert!(true);

    fn some_expensive_computation() -> bool { true } // a very simple function
    debug_assert!(some_expensive_computation());

    // assert with a custom message
    let x = true;
    debug_assert!(x, "x wasn't true!");

    let a = 3; let b = 27;
    debug_assert!(a + b == 30, "a = {}, b = {}", a, b);
}
