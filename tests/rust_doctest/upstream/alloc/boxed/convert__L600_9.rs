// Extracted from library/alloc/src/boxed/convert.rs:600
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::error::Error;
    
    let a_string_error = "a string error".to_string();
    let a_boxed_error = Box::<dyn Error + Send + Sync>::from(a_string_error);
    assert!(
        size_of::<Box<dyn Error + Send + Sync>>() == size_of_val(&a_boxed_error))
}
