const SMALL: u128 = 29;
const WIDE: u128 = (1_u128 << 100) + 7;
const NEXT: u128 = WIDE + 1;
const NEGATIVE: i128 = -(1_i128 << 100) + 3;
const POSITIVE: i128 = (1_i128 << 100) - 3;

fn unsigned(value: u128, tag: u8) -> u8 {
    match (value, tag) {
        (SMALL, 0) | (SMALL, 1) => 1,
        (WIDE, _) => 2,
        (NEXT, 0) | (NEXT, 2) => 3,
        _ => 4,
    }
}

fn signed(value: i128) -> u8 {
    match value {
        NEGATIVE => 5,
        POSITIVE => 6,
        -5 | 7 => 7,
        _ => 8,
    }
}

fn main() {
    assert_eq!(unsigned(SMALL, 0), 1);
    assert_eq!(unsigned(SMALL, 1), 1);
    assert_eq!(unsigned(SMALL, 5), 4);
    assert_eq!(unsigned(WIDE, 9), 2);
    assert_eq!(unsigned(NEXT, 0), 3);
    assert_eq!(unsigned(NEXT, 2), 3);
    assert_eq!(unsigned(NEXT, 1), 4);
    assert_eq!(unsigned(17, 0), 4);
    assert_eq!(signed(NEGATIVE), 5);
    assert_eq!(signed(POSITIVE), 6);
    assert_eq!(signed(-5), 7);
    assert_eq!(signed(7), 7);
    assert_eq!(signed(-6), 8);
}
