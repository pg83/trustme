fn main() {
    let cases: &[(&[u8], &[u8])] = &[
        (b"\xc0A", &[0xc0, 65]),
        (b"\xe0A", &[0xe0, 65]),
        (b"\xe0\x80A", &[0xe0, 0x80, 65]),
        (b"\xf0A", &[0xf0, 65]),
        (b"\xf0\x90A", &[0xf0, 0x90, 65]),
        (b"\xf0\x90\x80A", &[0xf0, 0x90, 0x80, 65]),
        (b"\xffA", &[0xff, 65]),
    ];
    for &(bytes, expected) in cases {
        assert_eq!(bytes, expected);
        assert!(std::str::from_utf8(bytes).is_err());
    }
    assert_eq!("𐐀🦀\u{10ffff}".chars().count(), 3);
}
