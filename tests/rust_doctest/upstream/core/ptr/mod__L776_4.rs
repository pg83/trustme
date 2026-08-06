// Extracted from library/core/src/ptr/mod.rs:776
#![allow(unused)]
fn main() {
    use std::ptr;
    use std::rc::Rc;
    
    let last = Rc::new(1);
    let weak = Rc::downgrade(&last);
    
    let mut v = vec![Rc::new(0), last];
    
    unsafe {
        // Get a raw pointer to the last element in `v`.
        let ptr = &mut v[1] as *mut _;
        // Shorten `v` to prevent the last item from being dropped. We do that first,
        // to prevent issues if the `drop_in_place` below panics.
        v.set_len(1);
        // Without a call `drop_in_place`, the last item would never be dropped,
        // and the memory it manages would be leaked.
        ptr::drop_in_place(ptr);
    }
    
    assert_eq!(v, &[0.into()]);
    
    // Ensure that the last item was dropped.
    assert!(weak.upgrade().is_none());
}
