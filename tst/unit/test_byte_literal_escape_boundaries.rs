fn main() {
    assert_eq!([b'\0', b'\x7f', b'\x80', b'\xff'], [0, 127, 128, 255]);
    assert_eq!(b"\0\x7f\x80\xFF", &[0, 127, 128, 255]);
    assert_eq!(b"a\
        \t\xFF", &[b'a', b'\t', 255]);
    assert_eq!(b"a\
        Z", b"aZ");
    assert_eq!(b"a\
        ", b"a");
    assert_eq!(br##"quote"#\u{41}"##, b"quote\"#\\u{41}");
    assert_eq!(br"ASCII\n", b"ASCII\\n");
    assert_eq!("λ\u{e9}", "λé");
    assert_eq!('\u{80}' as u32, 128);
    assert_eq!(c"λ\xFF".to_bytes(), &[0xce, 0xbb, 255]);
}
