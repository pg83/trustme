// Extracted from library/std/src/panic.rs:345
#![allow(unused)]
fn main() {
    use std::panic;
    
    let result = panic::catch_unwind(|| {
        println!("hello!");
    });
    assert!(result.is_ok());
    
    let result = panic::catch_unwind(|| {
        panic!("oh no!");
    });
    assert!(result.is_err());
}
