fn classify(value: char, allow: bool) -> u8 {
    match value { '\0'..='\u{10ffff}' if allow => 7, _ => 11 }
}
fn main() {
    for value in ['\0', 'λ', '\u{d7ff}', '\u{e000}', '\u{10ffff}'] {
        assert_eq!(classify(value, true), 7);
        assert_eq!(classify(value, false), 11);
    }
}
