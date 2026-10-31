fn classify(value: char, allow: bool) -> u8 {
    match value { '\u{3bb}' if allow => 7, 'α'..='ω' if allow => 11, '0'..='9' => 13, _ => 17 }
}
fn main() {
    assert_eq!(classify('λ', true), 7);
    assert_eq!(classify('λ', false), 17);
    assert_eq!(classify('ω', true), 11);
    assert_eq!(classify('0', false), 13);
    assert_eq!(classify('\u{10ffff}', true), 17);
}
