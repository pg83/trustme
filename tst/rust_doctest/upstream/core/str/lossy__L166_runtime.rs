// Extracted from library/core/src/str/lossy.rs:166
#![allow(unused)]
fn main() {
    fn from_utf8_lossy<F>(input: &[u8], mut push: F) where F: FnMut(&str) {
        for chunk in input.utf8_chunks() {
            push(chunk.valid());

            if !chunk.invalid().is_empty() {
                push("\u{FFFD}");
            }
        }
    }
}
