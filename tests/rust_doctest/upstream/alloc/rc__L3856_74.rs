// Extracted from library/alloc/src/rc.rs:3856
#![allow(unused)]
#![feature(unique_rc_arc)]
extern crate alloc;
fn main() {
    use std::rc::UniqueRc;
    use std::cmp::Ordering;
    
    let five = UniqueRc::new(5);
    
    assert_eq!(Some(Ordering::Less), five.partial_cmp(&UniqueRc::new(6)));
}
