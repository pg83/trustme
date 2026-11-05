fn main() {
    assert_eq!("\u{0041}\u{20ac}\u{1f642}", "A€🙂");
    assert_eq!('\u{1f642}', '🙂');
    assert_eq!("\u{1_f_6_4_2}".as_bytes(), &[0xf0, 0x9f, 0x99, 0x82]);
    assert_eq!(b"\x41", b"A");
}
