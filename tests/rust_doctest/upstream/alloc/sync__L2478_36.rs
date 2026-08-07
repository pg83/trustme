// Extracted from library/alloc/src/sync.rs:2478
#![allow(unused)]
#![feature(get_mut_unchecked)]
extern crate alloc;
fn main() {

    use std::sync::Arc;

    let mut x = Arc::new(String::new());
    unsafe {
        Arc::get_mut_unchecked(&mut x).push_str("foo")
    }
    assert_eq!(*x, "foo");
}
