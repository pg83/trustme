fn select(values: [u8; 40]) -> u8 {
    match values {
        [1 | 2, middle @ .., 3 | 4] => middle[0] + middle[37],
        [5, ..] => 23,
        _ => 0,
    }
}
fn bytes(values: &[u8]) -> u8 {
    match values { b"rust" => 17, [first, .., last] => first + last, _ => 0 }
}
fn main() {
    let mut values = [7; 40];
    values[0] = 2;
    values[39] = 4;
    assert_eq!(select(values), 14);
    values[0] = 5;
    assert_eq!(select(values), 23);
    assert_eq!(select([0; 40]), 0);
    assert_eq!(bytes(b"rust"), 17);
    assert_eq!(bytes(&[3, 7, 11]), 14);
    assert_eq!(bytes(&[]), 0);
}
