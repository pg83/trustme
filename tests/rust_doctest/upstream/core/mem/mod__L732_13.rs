// Extracted from library/core/src/mem/mod.rs:732
#![allow(unused)]
fn main() {
    use std::mem;
    
    let mut x = 5;
    let mut y = 42;
    
    mem::swap(&mut x, &mut y);
    
    assert_eq!(42, x);
    assert_eq!(5, y);
}
