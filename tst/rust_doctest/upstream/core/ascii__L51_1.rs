// Extracted from library/core/src/ascii.rs:51
#![allow(unused)]
fn main() {
    use std::ascii;

    let escaped = ascii::escape_default(b'0').next().unwrap();
    assert_eq!(b'0', escaped);

    let mut escaped = ascii::escape_default(b'\t');

    assert_eq!(b'\\', escaped.next().unwrap());
    assert_eq!(b't', escaped.next().unwrap());

    let mut escaped = ascii::escape_default(b'\r');

    assert_eq!(b'\\', escaped.next().unwrap());
    assert_eq!(b'r', escaped.next().unwrap());

    let mut escaped = ascii::escape_default(b'\n');

    assert_eq!(b'\\', escaped.next().unwrap());
    assert_eq!(b'n', escaped.next().unwrap());

    let mut escaped = ascii::escape_default(b'\'');

    assert_eq!(b'\\', escaped.next().unwrap());
    assert_eq!(b'\'', escaped.next().unwrap());

    let mut escaped = ascii::escape_default(b'"');

    assert_eq!(b'\\', escaped.next().unwrap());
    assert_eq!(b'"', escaped.next().unwrap());

    let mut escaped = ascii::escape_default(b'\\');

    assert_eq!(b'\\', escaped.next().unwrap());
    assert_eq!(b'\\', escaped.next().unwrap());

    let mut escaped = ascii::escape_default(b'\x9d');

    assert_eq!(b'\\', escaped.next().unwrap());
    assert_eq!(b'x', escaped.next().unwrap());
    assert_eq!(b'9', escaped.next().unwrap());
    assert_eq!(b'd', escaped.next().unwrap());
}
