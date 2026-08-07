// Extracted from library/alloc/src/rc.rs:3055
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Weak;

    let empty: Weak<i64> = Weak::new();
    assert!(empty.upgrade().is_none());
}
