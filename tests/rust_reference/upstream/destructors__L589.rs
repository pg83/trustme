// Extracted from src/destructors.md:589
#![allow(unused)]
fn main() {
    fn temp() {}
    // Final expressions of closures are not extending expressions.
    let x = || &temp(); // ERROR
    x;
}
