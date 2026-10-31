#[inline(never)]
fn inspect(value: &[u16]) -> u16 {
    match value {
        [3, first @ 5, middle @ .., last @ 11] => first + last + middle.len() as u16,
        [single] => *single,
        [] => 0,
        _ => 41,
    }
}
fn main() {
    assert_eq!(inspect(&[3, 5, 7, 11]), 17);
    assert_eq!(inspect(&[3, 5, 11]), 16);
    assert_eq!(inspect(&[19]), 19);
    assert_eq!(inspect(&[]), 0);
    assert_eq!(inspect(&[3, 7]), 41);
}
