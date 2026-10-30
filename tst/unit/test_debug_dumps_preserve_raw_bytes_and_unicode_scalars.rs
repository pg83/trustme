//@ compile-flags: -Zdump-ast -Zdump-hir -Zdump-mir
//@ output-contains: _1_ast.rs b"\xF0\x9F\xA6\x80\xFFAB"
//@ output-contains: _1_ast.rs b"\xF0X"
//@ output-contains: _1_ast.rs c"\xED\xA0\x80\xC0\xAF\xE0\x80\xAF\xF0\x80\x80\xAF\xF4\x90\x80\x80\xFF\x41\x42"
//@ output-contains: _2_hir.rs \u{3c0}\u{4e2d}\u{1f980}\u{10348}\u{10ffff}
//@ output-contains: _1_ast.rs \x00\x37\u{7f}
//@ output-contains: _2_hir.rs b"\xF0X"
//@ output-contains: _3_mir.rs b"\xF0\x9F\xA6\x80\xFFAB"
use std::ffi::CStr;
const ESCAPES: &str = "\n\\\"";
const EDGE: &str = "\07\x7f";
const TEXT: &str = "\u{3c0}\u{4e2d}\u{1f980}\u{10348}\u{10ffff}";
const BYTES: &[u8] = b"\xF0\x9F\xA6\x80\xFFAB";
const SHORT: &CStr = c"\xF0X\xF0\x90Y\xF0\x90\x80Z\xF0";
const INVALID: &CStr = c"\xED\xA0\x80\xC0\xAF\xE0\x80\xAF\xF0\x80\x80\xAF\xF4\x90\x80\x80\xFFAB";
fn classify(value: &[u8]) -> u8 { match value { b"\xF0X" => 7, _ => 9 } }
fn main() {
    assert_eq!(ESCAPES.as_bytes(), &[b'\n', b'\\', b'"']);
    assert_eq!(EDGE.as_bytes(), &[0, b'7', 0x7f]);
    assert_eq!(TEXT.len(), 17);
    assert_eq!(TEXT.chars().count(), 5);
    assert_eq!(BYTES, &[0xf0, 0x9f, 0xa6, 0x80, 0xff, b'A', b'B']);
    assert_eq!(SHORT.to_bytes(), &[0xf0, b'X', 0xf0, 0x90, b'Y', 0xf0, 0x90, 0x80, b'Z', 0xf0]);
    assert_eq!(INVALID.to_bytes(), &[0xed, 0xa0, 0x80, 0xc0, 0xaf, 0xe0, 0x80, 0xaf, 0xf0, 0x80, 0x80, 0xaf, 0xf4, 0x90, 0x80, 0x80, 0xff, b'A', b'B']);
    assert!(std::str::from_utf8(INVALID.to_bytes()).is_err());
    assert_eq!(classify(&[0xf0, b'X']), 7);
    assert_eq!(classify(b"X"), 9);
}
