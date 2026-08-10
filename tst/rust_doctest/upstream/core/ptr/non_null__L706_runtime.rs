// Extracted from library/core/src/ptr/non_null.rs:706
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;

    let s: &str = "123";

    unsafe {
        let end: NonNull<u8> = NonNull::new(s.as_ptr().cast_mut()).unwrap().add(3);
        println!("{}", end.sub(1).read() as char);
        println!("{}", end.sub(2).read() as char);
    }
}
