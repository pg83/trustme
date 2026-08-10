#[inline(never)]
fn select(value: u128) -> u8 {
    match value {
        0 => 0,
        0x7fff_0000_0000_0000_0000_0000_0000_0000 => 1,
        _ => 2,
    }
}

fn main() {
    assert_eq!(select(0x7fff_0000_0000_0000_0000_0000_0000_0000), 1);
    assert_eq!(select(0x8000_0000_0000_0000_0000_0000_0000_0000), 2);
}
