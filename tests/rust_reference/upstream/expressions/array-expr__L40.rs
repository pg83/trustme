// Extracted from src/expressions/array-expr.md:40
#![allow(unused)]
fn main() {
    const C: usize = 1;
    let _: [u8; C] = [0; 1]; // Literal.
    let _: [u8; C] = [0; C]; // Constant item.
    let _: [u8; C] = [0; _]; // Inferred const.
    let _: [u8; C] = [0; (((_)))]; // Inferred const.
}
