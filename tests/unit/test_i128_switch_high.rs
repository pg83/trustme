#[inline(never)]
fn select(value: i128) -> u8 {
    match value {
        -1 => 0,
        0 => 1,
        _ => 2,
    }
}

fn main() {
    assert_eq!(select(1i128 << 64), 2);
    assert_eq!(select(-(1i128 << 64)), 2);
}
