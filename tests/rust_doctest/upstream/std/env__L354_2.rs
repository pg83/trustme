// Extracted from library/std/src/env.rs:354
#![allow(unused)]
fn main() {
    use std::env;
    
    let key = "KEY";
    unsafe {
        env::set_var(key, "VALUE");
    }
    assert_eq!(env::var(key), Ok("VALUE".to_string()));
}
