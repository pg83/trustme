// Extracted from src/destructors.md:603
#![allow(unused)]
fn main() {
    fn temp() {}
    // Operands of breaks to labels are not extending expressions.
    let x = 'a: { break 'a &temp() }; // ERROR
    x;
}
