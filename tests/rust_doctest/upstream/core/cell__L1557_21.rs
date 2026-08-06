// Extracted from library/core/src/cell.rs:1557
#![allow(unused)]
fn main() {
    use std::cell::{RefCell, Ref};
    
    let c = RefCell::new(vec![1, 2, 3]);
    let b1: Ref<'_, Vec<u32>> = c.borrow();
    let b2: Result<Ref<'_, u32>, _> = Ref::filter_map(b1, |v| v.get(1));
    assert_eq!(*b2.unwrap(), 2);
}
