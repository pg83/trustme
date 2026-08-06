// Extracted from src/destructors.md:596
#![allow(unused)]
fn main() {
    fn temp() {}
    // Operands of loop breaks are not extending expressions.
    let x = loop { break &temp() }; // ERROR
    x;
}
