// Extracted from library/alloc/src/boxed/convert.rs:666
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::error::Error;
    
    let a_str_error = "a str error";
    let a_boxed_error = Box::<dyn Error + Send + Sync>::from(a_str_error);
    assert!(
        size_of::<Box<dyn Error + Send + Sync>>() == size_of_val(&a_boxed_error))
}
