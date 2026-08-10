// Extracted from src/items/enumerations.md:316
#![allow(unused)]
fn main() {
    enum ZeroVariants {}
    let x: ZeroVariants = panic!();
    let y: u32 = x; // mismatched type error
}
