// Extracted from library/alloc/src/boxed/convert.rs:238
#![allow(unused)]
extern crate alloc;
fn main() {
    // create a Box<str> which will be used to create a Box<[u8]>
    let boxed: Box<str> = Box::from("hello");
    let boxed_str: Box<[u8]> = Box::from(boxed);
    
    // create a &[u8] which will be used to create a Box<[u8]>
    let slice: &[u8] = &[104, 101, 108, 108, 111];
    let boxed_slice = Box::from(slice);
    
    assert_eq!(boxed_slice, boxed_str);
}
