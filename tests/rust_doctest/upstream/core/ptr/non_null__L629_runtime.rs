// Extracted from library/core/src/ptr/non_null.rs:629
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;
    
    let s: &str = "123";
    let ptr: NonNull<u8> = NonNull::new(s.as_ptr().cast_mut()).unwrap();
    
    unsafe {
        println!("{}", ptr.add(1).read() as char);
        println!("{}", ptr.add(2).read() as char);
    }
}
