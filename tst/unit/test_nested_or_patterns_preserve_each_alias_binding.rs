#[inline(never)]
fn inspect(value: Option<(u8, u8)>) -> u8 {
    match value {
        Some((first @ (3 | 5), last)) => first + last,
        Some((_, last @ (7 | 11))) => last,
        _ => 0,
    }
}
fn main() {
    assert_eq!(inspect(Some((3, 13))), 16);
    assert_eq!(inspect(Some((5, 17))), 22);
    assert_eq!(inspect(Some((19, 7))), 7);
    assert_eq!(inspect(Some((23, 11))), 11);
    assert_eq!(inspect(None), 0);
}
