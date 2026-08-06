// Extracted from library/core/src/primitive_docs.rs:369
#![allow(unused)]
fn main() {
    let c: char = 'a';
    match c {
        '\0' ..= '\u{D7FF}' => false,
        '\u{E000}' ..= '\u{10FFFF}' => true,
    };
}
