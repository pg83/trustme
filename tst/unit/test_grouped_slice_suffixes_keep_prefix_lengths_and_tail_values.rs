#[inline(never)]
fn classify(value: &[u16]) -> u8 {
    match value { [_, .., 37] => 7, [.., 41] => 11, [] => 13, _ => 17 }
}
fn main() {
    assert_eq!(classify(&[]), 13);
    assert_eq!(classify(&[37]), 17);
    assert_eq!(classify(&[41]), 11);
    assert_eq!(classify(&[5, 37]), 7);
    assert_eq!(classify(&[7, 8, 37]), 7);
    assert_eq!(classify(&[5, 41]), 11);
    assert_eq!(classify(&[5, 43]), 17);
}
