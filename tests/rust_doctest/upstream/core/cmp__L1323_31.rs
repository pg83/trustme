// Extracted from library/core/src/cmp.rs:1323
#![allow(unused)]
fn main() {
    let x: u32 = 0;
    let y: u32 = 1;
    
    assert_eq!(x < y, true);
    assert_eq!(x.lt(&y), true);
}
