fn classify(value: ((u8, u8), u8)) -> u8 {
    match value {
        ((0 | 1, 2) | (3, 4), 5) => 11,
        _ => 7,
    }
}

fn main() {
    assert_eq!(classify(((0, 2), 5)), 11);
    assert_eq!(classify(((1, 2), 5)), 11);
    assert_eq!(classify(((3, 4), 5)), 11);
    assert_eq!(classify(((3, 2), 5)), 7);
    assert_eq!(classify(((0, 4), 5)), 7);
    assert_eq!(classify(((0, 2), 6)), 7);
}
