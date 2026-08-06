// Extracted from src/destructors.md:582
#![allow(unused)]
fn main() {
    fn temp() {}
    // Final expressions of `async` blocks are not extending expressions.
    let x = async { &temp() }; // ERROR
    x;
}
