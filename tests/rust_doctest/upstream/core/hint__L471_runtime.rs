// Extracted from library/core/src/hint.rs:471
#![allow(unused)]
fn main() {
    use std::hint::black_box;
    
    // No assumptions can be made about either operand, so the multiplication is not optimized out.
    let y = black_box(5) * black_box(10);
}
