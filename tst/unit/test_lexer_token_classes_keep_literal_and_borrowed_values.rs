/// Keep lifetime tokens distinct from character literals.
fn borrowed<'a>(value: &'a u16) -> &'a u16 {
    value
}

fn main() {
    let число = 0x4f9_u16;
    let tuple = (число, [3u8, 5u8, 7u8]);
    assert_eq!(*borrowed(&tuple.0), 1273);
    assert_eq!(tuple.1[2], 7);

    /* A block comment /* with an inner comment */ ends here. */
    assert_eq!(b'\x41', b'A');
    assert_eq!('\u{03bb}', 'λ');
    assert_eq!(r##"edge"#inside"##, "edge\"#inside");
    assert_eq!(br#"a"b"#, b"a\"b");
    assert_eq!(c"tag".to_bytes(), b"tag");
    assert_eq!(1.25e1f64, 12.5f64);
}
