const LEFT: &str = "left";
const RIGHT: &str = "right";
const WIDE: u128 = (1_u128 << 96) + 5;
const NEGATIVE: i128 = -(1_i128 << 96) + 7;
const POSITIVE: i128 = (1_i128 << 96) + 7;

fn classify(text: &str, value: u128, signed: i128) -> u8 {
    match (text, value, signed) {
        (LEFT, WIDE, NEGATIVE) => 1,
        (LEFT, WIDE, POSITIVE) => 2,
        (LEFT, 3, _) => 3,
        (RIGHT, WIDE, _) => 4,
        _ => 5,
    }
}

fn main() {
    assert_eq!(classify("left", WIDE, NEGATIVE), 1);
    assert_eq!(classify("left", WIDE, POSITIVE), 2);
    assert_eq!(classify("left", 3, 17), 3);
    assert_eq!(classify("right", WIDE, 0), 4);
    assert_eq!(classify("left", WIDE, 0), 5);
    assert_eq!(classify("left", 2, NEGATIVE), 5);
    assert_eq!(classify("right", 3, 0), 5);
    assert_eq!(classify("other", WIDE, NEGATIVE), 5);
}
