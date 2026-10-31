const FIRST: &[u8] = &[1, 3];
const SECOND: &[u8] = &[5, 7, 9];
fn classify(bytes: &[u8]) -> u8 {
    match bytes { FIRST => 1, SECOND => 2, [11, middle @ .., 17] => middle.len() as u8 + 3, [] => 7, _ => 0 }
}
fn main() {
    assert_eq!(classify(&[1, 3]), 1);
    assert_eq!(classify(&[5, 7, 9]), 2);
    assert_eq!(classify(&[11, 17]), 3);
    assert_eq!(classify(&[11, 13, 15, 17]), 5);
    assert_eq!(classify(&[]), 7);
    assert_eq!(classify(&[1, 4]), 0);
}
