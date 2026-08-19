//@ run-pass
// `stringify!` re-escapes a string literal byte by byte: printable ASCII goes
// through as itself, a valid UTF-8 sequence stays one character, a control
// byte becomes `\u{..}`, and a byte that starts no sequence becomes `\xHH`.
// A byte string has no text in it, so every byte that is not printable ASCII
// is written as a hex escape.

fn main() {
    assert_eq!(stringify!("plain"), "\"plain\"");
    assert_eq!(stringify!("\u{7}"), "\"\\u{7}\"");
    assert_eq!(stringify!("tab\there"), "\"tab\\there\"");
    assert_eq!(stringify!("a\r\nb"), "\"a\\r\\nb\"");
    assert_eq!(stringify!("quote\" and \\"), "\"quote\\\" and \\\\\"");
    assert_eq!(stringify!("crab 🦀"), "\"crab 🦀\"");
    assert_eq!(stringify!(c"\xFErris the 🦀\u{7}"), "c\"\\xFErris the 🦀\\u{7}\"");
    assert_eq!(stringify!(b"\xFE"), "b\"\\xFE\"");
    assert_eq!(stringify!(b"\x00"), "b\"\\x00\"");
    assert_eq!(stringify!(b"tab\there"), "b\"tab\\there\"");
}
