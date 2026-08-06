// Extracted from library/core/src/cell/lazy.rs:99
#![allow(unused)]
fn main() {
    use std::cell::LazyCell;
    
    let lazy = LazyCell::new(|| 92);
    
    assert_eq!(LazyCell::force(&lazy), &92);
    assert_eq!(&*lazy, &92);
}
