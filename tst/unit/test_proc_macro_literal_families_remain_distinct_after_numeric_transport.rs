//@ proc-macro-aux-build: numeric_token_bridge.rs
use numeric_token_bridge::echo;
fn main() {
    assert_eq!(echo!(b'Z'), 90u8);
    assert_eq!(echo!('\0'), '\0');
    assert_eq!(echo!('λ'), 'λ');
    assert_eq!(echo!('\u{1f642}'), '\u{1f642}');
    assert_eq!(echo!("a\0b"), "a\0b");
    assert_eq!(echo!(b"\0\xff"), &[0u8, 255]);
    assert_eq!(echo!(r##"raw\literal"##), "raw\\literal");
    assert_eq!(echo!(c"bridge").to_bytes(), b"bridge");
}
