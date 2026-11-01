fn pick(first: &u16, second: &u16) -> u16 {
    match (first, second) {
        (&3, &5) => 7,
        (&11, _) => 13,
        _ => 17,
    }
}
fn main() {
    assert_eq!(pick(&3, &5), 7);
    assert_eq!(pick(&11, &19), 13);
    assert_eq!(pick(&23, &29), 17);
}
