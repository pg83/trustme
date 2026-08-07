// Extracted from library/core/src/str/lossy.rs:57
#![allow(unused)]
fn main() {
    // An invalid UTF-8 string
    let bytes = b"foo\xF1\x80bar";

    // Decode the first `Utf8Chunk`
    let chunk = bytes.utf8_chunks().next().unwrap();

    // The first three characters are valid UTF-8
    assert_eq!("foo", chunk.valid());

    // The fourth character is broken
    assert_eq!(b"\xF1\x80", chunk.invalid());
}
