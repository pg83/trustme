// Extracted from library/core/src/mem/mod.rs:456
#![allow(unused)]
#![allow(deprecated)]
fn main() {
    use std::mem;
    
    assert_eq!(4, mem::min_align_of_val(&5i32));
}
