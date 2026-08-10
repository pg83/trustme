// Extracted from library/std/src/io/mod.rs:2428
#![allow(unused)]
fn main() {
    use std::io::{self, BufRead};

    let mut cursor = io::Cursor::new(b"lorem-ipsum");
    let mut buf = vec![];

    // cursor is at 'l'
    let num_bytes = cursor.read_until(b'-', &mut buf)
        .expect("reading from cursor won't fail");
    assert_eq!(num_bytes, 6);
    assert_eq!(buf, b"lorem-");
    buf.clear();

    // cursor is at 'i'
    let num_bytes = cursor.read_until(b'-', &mut buf)
        .expect("reading from cursor won't fail");
    assert_eq!(num_bytes, 5);
    assert_eq!(buf, b"ipsum");
    buf.clear();

    // cursor is at EOF
    let num_bytes = cursor.read_until(b'-', &mut buf)
        .expect("reading from cursor won't fail");
    assert_eq!(num_bytes, 0);
    assert_eq!(buf, b"");
}
