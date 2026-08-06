// Extracted from library/core/src/num/mod.rs:1056
#![allow(unused)]
fn main() {
    assert_eq!("0", b'0'.escape_ascii().to_string());
    assert_eq!("\\t", b'\t'.escape_ascii().to_string());
    assert_eq!("\\r", b'\r'.escape_ascii().to_string());
    assert_eq!("\\n", b'\n'.escape_ascii().to_string());
    assert_eq!("\\'", b'\''.escape_ascii().to_string());
    assert_eq!("\\\"", b'"'.escape_ascii().to_string());
    assert_eq!("\\\\", b'\\'.escape_ascii().to_string());
    assert_eq!("\\x9d", b'\x9d'.escape_ascii().to_string());
}
