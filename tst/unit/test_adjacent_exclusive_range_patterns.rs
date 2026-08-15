fn classify(value: i32) -> u8 {
    match value {
        -3..0 => 1,
        0..5 => 2,
        _ => 3,
    }
}

fn main() {
    assert_eq!(classify(-1), 1);
    assert_eq!(classify(0), 2);
    assert_eq!(classify(5), 3);
}
