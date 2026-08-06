// Extracted from library/core/src/macros/mod.rs:32
#![allow(unused)]
fn main() {
    let a = 3;
    let b = 1 + 2;
    assert_eq!(a, b);
    
    assert_eq!(a, b, "we are testing addition with {} and {}", a, b);
}
