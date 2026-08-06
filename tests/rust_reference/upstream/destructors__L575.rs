// Extracted from src/destructors.md:575
#![allow(unused)]
fn main() {
    fn temp() {}
    // Scrutinees of match expressions are not extending expressions.
    let x = match &temp() { x => x }; // ERROR
    x;
}
