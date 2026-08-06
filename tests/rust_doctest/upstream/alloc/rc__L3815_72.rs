// Extracted from library/alloc/src/rc.rs:3815
#![allow(unused)]
#![feature(unique_rc_arc)]
extern crate alloc;
fn main() {
    use std::rc::UniqueRc;
    
    let five = UniqueRc::new(5);
    
    assert!(five == UniqueRc::new(5));
}
