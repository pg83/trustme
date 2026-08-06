// Extracted from library/alloc/src/sync.rs:4271
#![allow(unused)]
#![feature(unique_rc_arc)]
extern crate alloc;
fn main() {
    use std::sync::UniqueArc;
    use std::cmp::Ordering;
    
    let five = UniqueArc::new(5);
    
    assert_eq!(Some(Ordering::Less), five.partial_cmp(&UniqueArc::new(6)));
}
