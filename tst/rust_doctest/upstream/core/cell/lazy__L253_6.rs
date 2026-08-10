// Extracted from library/core/src/cell/lazy.rs:253
#![allow(unused)]
#![feature(lazy_get)]
fn main() {

    use std::cell::LazyCell;

    let lazy = LazyCell::new(|| 92);

    assert_eq!(LazyCell::get(&lazy), None);
    let _ = LazyCell::force(&lazy);
    assert_eq!(LazyCell::get(&lazy), Some(&92));
}
