// Extracted from src/destructors.md:558
#![allow(unused)]
fn main() {
    fn temp() {}
    // Arguments to function calls are not extending expressions. The
    // temporary is dropped at the semicolon.
    let x = core::convert::identity(&temp()); // ERROR
    x;
}
