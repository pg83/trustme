// Extracted from library/alloc/src/boxed.rs:940
#![allow(unused)]
extern crate alloc;
fn main() {
    let big_box = Box::<[usize; 1024]>::new_uninit();
    
    let mut array = [0; 1024];
    for (i, place) in array.iter_mut().enumerate() {
        *place = i;
    }
    
    // The optimizer may be able to elide this copy, so previous code writes
    // to heap directly.
    let big_box = Box::write(big_box, array);
    
    for (i, x) in big_box.iter().enumerate() {
        assert_eq!(*x, i);
    }
}
