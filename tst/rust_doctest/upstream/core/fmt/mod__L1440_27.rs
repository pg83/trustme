// Extracted from library/core/src/fmt/mod.rs:1440
#![allow(unused)]
fn main() {
    use std::fmt::Write;

    let mut output = String::new();
    write!(&mut output, "Hello {}!", "world")
        .expect("Error occurred while trying to write in String");
    assert_eq!(output, "Hello world!");
}
