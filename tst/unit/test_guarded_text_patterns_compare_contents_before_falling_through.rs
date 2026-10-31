fn classify(value: &str, allow: bool) -> u8 {
    match value { "route" if allow => 7, "route" => 11, "λ" => 13, _ => 17 }
}
fn main() {
    let owned = String::from("route");
    assert_eq!(classify(&owned, true), 7);
    assert_eq!(classify(&owned, false), 11);
    assert_eq!(classify("λ", true), 13);
    assert_eq!(classify("", true), 17);
}
