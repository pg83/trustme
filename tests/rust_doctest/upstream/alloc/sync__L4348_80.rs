// Extracted from library/alloc/src/sync.rs:4348
#![allow(unused)]
#![feature(unique_rc_arc)]
extern crate alloc;
fn main() {
    use std::sync::UniqueArc;
    
    let five = UniqueArc::new(5);
    
    assert!(five >= UniqueArc::new(5));
}
