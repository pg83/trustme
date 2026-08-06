// Extracted from library/std/src/ascii.rs:27
#![allow(unused)]
fn main() {
    use std::ascii::AsciiExt;
    
    assert_eq!(AsciiExt::to_ascii_uppercase("café"), "CAFÉ");
    assert_eq!(AsciiExt::to_ascii_uppercase("café"), "CAFé");
}
