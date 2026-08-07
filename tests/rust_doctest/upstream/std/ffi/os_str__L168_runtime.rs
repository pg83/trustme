// Extracted from library/std/src/ffi/os_str.rs:168
#![allow(unused)]
fn main() {
    use std::ffi::OsStr;

    let os_str = OsStr::new("Mary had a little lamb");
    let bytes = os_str.as_encoded_bytes();
    let words = bytes.split(|b| *b == b' ');
    let words: Vec<&OsStr> = words.map(|word| {
        // SAFETY:
        // - Each `word` only contains content that originated from `OsStr::as_encoded_bytes`
        // - Only split with ASCII whitespace which is a non-empty UTF-8 substring
        unsafe { OsStr::from_encoded_bytes_unchecked(word) }
    }).collect();
}
