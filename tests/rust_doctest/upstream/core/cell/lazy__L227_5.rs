// Extracted from library/core/src/cell/lazy.rs:227
#![allow(unused)]
#![feature(lazy_get)]
fn main() {

    use std::cell::LazyCell;

    let mut lazy = LazyCell::new(|| 92);

    assert_eq!(LazyCell::get_mut(&mut lazy), None);
    let _ = LazyCell::force(&lazy);
    *LazyCell::get_mut(&mut lazy).unwrap() = 44;
    assert_eq!(*lazy, 44);
}
