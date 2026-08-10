// Extracted from library/alloc/src/sync.rs:4370
#![allow(unused)]
#![feature(unique_rc_arc)]
extern crate alloc;
fn main() {
    use std::sync::UniqueArc;
    use std::cmp::Ordering;

    let five = UniqueArc::new(5);

    assert_eq!(Ordering::Less, five.cmp(&UniqueArc::new(6)));
}
