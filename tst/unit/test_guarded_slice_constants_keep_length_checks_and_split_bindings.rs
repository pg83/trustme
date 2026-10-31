const WORDS: &[u16] = &[37, 41];
fn classify(value: &[u16], allow: bool) -> usize {
    match value { WORDS if allow => 7, [37, rest @ .., 43] if allow => rest.len() + 11, [41] => 17, _ => 19 }
}
fn bytes(value: &[u8], allow: bool) -> u8 {
    match value { b"key" if allow => 7, [b'k', ..] => 11, _ => 13 }
}
fn main() {
    assert_eq!(classify(WORDS, true), 7);
    assert_eq!(classify(WORDS, false), 19);
    assert_eq!(classify(&[37, 2, 3, 43], true), 13);
    assert_eq!(classify(&[41], false), 17);
    assert_eq!(classify(&[], true), 19);
    assert_eq!(bytes(b"key", true), 7);
    assert_eq!(bytes(b"key", false), 11);
    assert_eq!(bytes(b"", true), 13);
}
