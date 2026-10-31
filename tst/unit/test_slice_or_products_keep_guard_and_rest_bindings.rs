fn select(values: &[Option<u8>]) -> u8 {
    match values {
        [Some(1 | 2), middle @ .., Some(3 | 4)] if !middle.is_empty() => 17,
        [Some(5 | 6), ..] => 23,
        [Some(1 | 2), Some(3 | 4)] => 29,
        _ => 0,
    }
}
fn main() {
    assert_eq!(select(&[Some(1), None, Some(4)]), 17);
    assert_eq!(select(&[Some(2), Some(9), Some(3)]), 17);
    assert_eq!(select(&[Some(2), Some(4)]), 29);
    assert_eq!(select(&[Some(6)]), 23);
    assert_eq!(select(&[]), 0);
    assert_eq!(select(&[None, Some(4)]), 0);
}
