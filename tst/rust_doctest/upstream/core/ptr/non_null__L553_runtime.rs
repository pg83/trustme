// Extracted from library/core/src/ptr/non_null.rs:553
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;

    let mut s = [1, 2, 3];
    let ptr: NonNull<u32> = NonNull::new(s.as_mut_ptr()).unwrap();

    unsafe {
        println!("{}", ptr.offset(1).read());
        println!("{}", ptr.offset(2).read());
    }
}
