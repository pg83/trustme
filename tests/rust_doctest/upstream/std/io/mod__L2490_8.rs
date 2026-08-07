// Extracted from library/std/src/io/mod.rs:2490
#![allow(unused)]
fn main() {
    use std::io::{self, BufRead};

    let mut cursor = io::Cursor::new(b"Ferris\0Likes long walks on the beach\0Crustacean\0");

    // read name
    let mut name = Vec::new();
    let num_bytes = cursor.read_until(b'\0', &mut name)
        .expect("reading from cursor won't fail");
    assert_eq!(num_bytes, 7);
    assert_eq!(name, b"Ferris\0");

    // skip fun fact
    let num_bytes = cursor.skip_until(b'\0')
        .expect("reading from cursor won't fail");
    assert_eq!(num_bytes, 30);

    // read animal type
    let mut animal = Vec::new();
    let num_bytes = cursor.read_until(b'\0', &mut animal)
        .expect("reading from cursor won't fail");
    assert_eq!(num_bytes, 11);
    assert_eq!(animal, b"Crustacean\0");
}
