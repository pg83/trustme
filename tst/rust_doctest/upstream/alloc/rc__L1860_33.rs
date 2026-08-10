// Extracted from library/alloc/src/rc.rs:1860
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::rc::Rc;

    let mut data = Rc::new(5);

    *Rc::make_mut(&mut data) += 1;         // Won't clone anything
    let mut other_data = Rc::clone(&data); // Won't clone inner data
    *Rc::make_mut(&mut data) += 1;         // Clones inner data
    *Rc::make_mut(&mut data) += 1;         // Won't clone anything
    *Rc::make_mut(&mut other_data) *= 2;   // Won't clone anything

    // Now `data` and `other_data` point to different allocations.
    assert_eq!(*data, 8);
    assert_eq!(*other_data, 12);
}
