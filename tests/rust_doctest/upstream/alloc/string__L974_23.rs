// Extracted from library/alloc/src/string.rs:974
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::mem;
    
    unsafe {
        let s = String::from("hello");
    
    
        // Prevent automatically dropping the String's data
        let mut s = mem::ManuallyDrop::new(s);
    
        let ptr = s.as_mut_ptr();
        let len = s.len();
        let capacity = s.capacity();
    
        let s = String::from_raw_parts(ptr, len, capacity);
    
        assert_eq!(String::from("hello"), s);
    }
}
