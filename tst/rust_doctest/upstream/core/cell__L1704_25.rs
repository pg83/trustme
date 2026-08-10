// Extracted from library/core/src/cell.rs:1704
#![allow(unused)]
fn main() {
    use std::cell::{RefCell, RefMut};

    let c = RefCell::new(vec![1, 2, 3]);

    {
        let b1: RefMut<'_, Vec<u32>> = c.borrow_mut();
        let mut b2: Result<RefMut<'_, u32>, _> = RefMut::filter_map(b1, |v| v.get_mut(1));

        if let Ok(mut b2) = b2 {
            *b2 += 2;
        }
    }

    assert_eq!(*c.borrow(), vec![1, 4, 3]);
}
