// Extracted from library/core/src/ptr/mod.rs:1668
#![allow(unused)]
fn main() {
    use std::ptr;
    
    let mut s = String::from("foo");
    unsafe {
        // `s2` now points to the same underlying memory as `s`.
        let mut s2: String = ptr::read(&s);
    
        assert_eq!(s2, "foo");
    
        // Assigning to `s2` causes its original value to be dropped. Beyond
        // this point, `s` must no longer be used, as the underlying memory has
        // been freed.
        s2 = String::default();
        assert_eq!(s2, "");
    
        // Assigning to `s` would cause the old value to be dropped again,
        // resulting in undefined behavior.
        // s = String::from("bar"); // ERROR
    
        // `ptr::write` can be used to overwrite a value without dropping it.
        ptr::write(&mut s, String::from("bar"));
    }
    
    assert_eq!(s, "bar");
}
