#![allow(unreachable_code)]

fn pick() -> u8 {
    let values = [11u8, 22u8];
    values[(return 37) as usize]
}

fn main() {
    assert_eq!(pick(), 37);
}
