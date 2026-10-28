#![feature(const_cmp)]

const fn text_tag(value: &str) -> u8 {
    match value {
        "" => 1,
        "a" => 2,
        "alphabet" => 3,
        "λ" => 4,
        _ => 5,
    }
}

const fn bytes_tag(value: &[u8]) -> u8 {
    match value {
        b"" => 6,
        b"a" => 7,
        b"alphabet" => 8,
        b"\x00\xff" => 9,
        _ => 10,
    }
}

const fn array_tag(value: &[u8; 2]) -> u8 {
    match value {
        b"ab" => 11,
        b"cd" => 12,
        _ => 13,
    }
}

const TEXT: [u8; 7] = [text_tag(""), text_tag("a"), text_tag("alphabet"),
    text_tag("λ"), text_tag("alph"), text_tag("alphabetical"), text_tag("z")];
const BYTES: [u8; 7] = [bytes_tag(b""), bytes_tag(b"a"), bytes_tag(b"alphabet"),
    bytes_tag(b"\x00\xff"), bytes_tag(b"alph"), bytes_tag(b"alphabetical"), bytes_tag(b"z")];
const ARRAYS: [u8; 3] = [array_tag(b"ab"), array_tag(b"cd"), array_tag(b"ef")];
const OFFSET: &[u8] = unsafe { std::slice::from_raw_parts(b"-alphabet".as_ptr().add(1), 8) };
const OFFSET_TAG: u8 = bytes_tag(OFFSET);
const EMPTY: &[u8] = unsafe { std::slice::from_raw_parts(1usize as *const u8, 0) };
const EMPTY_BYTES_TAG: u8 = bytes_tag(EMPTY);
const EMPTY_TEXT_TAG: u8 = text_tag(unsafe { std::str::from_utf8_unchecked(EMPTY) });

fn main() {
    assert_eq!(TEXT, [1, 2, 3, 4, 5, 5, 5]);
    assert_eq!(BYTES, [6, 7, 8, 9, 10, 10, 10]);
    assert_eq!(ARRAYS, [11, 12, 13]);
    assert_eq!(OFFSET_TAG, 8);
    assert_eq!(EMPTY_BYTES_TAG, 6);
    assert_eq!(EMPTY_TEXT_TAG, 1);
    for (i, text) in ["", "a", "alphabet", "λ", "alph", "alphabetical", "z"].iter().enumerate() {
        assert_eq!(text_tag(std::hint::black_box(text)), TEXT[i]);
    }
}
