fn unsigned(value: u128, accept: bool) -> u8 {
    match value {
        0..=3 if accept => 1,
        5..9 if accept => 2,
        10..=u128::MAX if accept => 3,
        _ => 0,
    }
}
fn signed(value: i128, accept: bool) -> u8 {
    match value {
        i128::MIN..=-3 if accept => 1,
        -1..2 if accept => 2,
        3..=i128::MAX if accept => 3,
        _ => 0,
    }
}
fn character(value: char, accept: bool) -> u8 {
    match value {
        '\0'..='c' if accept => 1,
        'e'..'h' if accept => 2,
        'z'..='\u{10ffff}' if accept => 3,
        _ => 0,
    }
}
fn main() {
    for (value, expected) in [(0, 1), (3, 1), (4, 0), (5, 2), (8, 2), (9, 0), (10, 3), (u128::MAX, 3)] {
        assert_eq!(unsigned(value, true), expected);
        assert_eq!(unsigned(value, false), 0);
    }
    for (value, expected) in [(i128::MIN, 1), (-3, 1), (-2, 0), (-1, 2), (1, 2), (2, 0), (3, 3), (i128::MAX, 3)] {
        assert_eq!(signed(value, true), expected);
        assert_eq!(signed(value, false), 0);
    }
    for (value, expected) in [('\0', 1), ('c', 1), ('d', 0), ('e', 2), ('g', 2), ('h', 0), ('z', 3), ('\u{10ffff}', 3)] {
        assert_eq!(character(value, true), expected);
        assert_eq!(character(value, false), 0);
    }
}
