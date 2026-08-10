// Extracted from library/core/src/fmt/mod.rs:1429
#![allow(unused)]
fn main() {
    use std::fmt;

    let mut output = String::new();
    fmt::write(&mut output, format_args!("Hello {}!", "world"))
        .expect("Error occurred while trying to write in String");
    assert_eq!(output, "Hello world!");
}
