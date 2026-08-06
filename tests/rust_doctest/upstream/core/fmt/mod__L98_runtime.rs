// Extracted from library/core/src/fmt/mod.rs:98
#![allow(unused)]
fn main() {
    use std::fmt::{self, write};
    
    let mut output = String::new();
    if let Err(fmt::Error) = write(&mut output, format_args!("Hello {}!", "world")) {
        panic!("An error occurred");
    }
}
