// Extracted from library/alloc/src/rc.rs:3528
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Weak;
    
    let empty: Weak<i64> = Default::default();
    assert!(empty.upgrade().is_none());
}
