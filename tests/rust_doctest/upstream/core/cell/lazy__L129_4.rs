// Extracted from library/core/src/cell/lazy.rs:129
#![allow(unused)]
#![feature(lazy_get)]
fn main() {
    use std::cell::LazyCell;
    
    let mut lazy = LazyCell::new(|| 92);
    
    let p = LazyCell::force_mut(&mut lazy);
    assert_eq!(*p, 92);
    *p = 44;
    assert_eq!(*lazy, 44);
}
