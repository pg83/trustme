fn classify(value: (f64, u8)) -> u8 {
    match value {
        (1.0, 0) => 10,
        (1.0, 1) => 11,
        _ => 12,
    }
}

fn main() {
    assert_eq!(classify((1.0, 0)), 10);
    assert_eq!(classify((1.0, 1)), 11);
    assert_eq!(classify((1.0, 2)), 12);
    assert_eq!(classify((0.0, 0)), 12);
    assert_eq!(classify((2.0, 1)), 12);
}
