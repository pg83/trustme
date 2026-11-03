fn main() {
    assert_eq!(format!("{:é>5}", 7), "éééé7");
    assert_eq!(format!("{:ø^6}", 11), "øø11øø");
    assert_eq!(format!("{:ß<4}", "x"), "xßßß");
    let width = 5;
    assert_eq!(format!("{:ä>width$.2}", "abcd"), "äääab");
}
