// Extracted from library/alloc/src/boxed/convert.rs:729
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::error::Error;
    use std::borrow::Cow;
    
    let a_cow_str_error = Cow::from("a str error");
    let a_boxed_error = Box::<dyn Error>::from(a_cow_str_error);
    assert!(size_of::<Box<dyn Error>>() == size_of_val(&a_boxed_error))
}
