//@ compile-variant: -O
#[inline(never)]
fn select<const OFFSET: u16>(value: u8) -> u16 {
    match value {
        3 => OFFSET,
        7 | 9 => OFFSET + 1,
        128..=131 => OFFSET + 2,
        _ => OFFSET + 3,
    }
}
fn main() {
    assert_eq!(select::<17>(3), 17);
    assert_eq!(select::<17>(7), 18);
    assert_eq!(select::<17>(9), 18);
    assert_eq!(select::<17>(130), 19);
    assert_eq!(select::<17>(255), 20);
    assert_eq!(select::<31>(0), 34);
}
