//@ compile-variant: -O

const fn selected(bytes: &[u8]) -> u8 {
    bytes[0] + bytes[bytes.len() - 1]
}

const BYTES: [u8; 3] = [3, 5, 7];
const TOTAL: u8 = selected(&BYTES);

fn main() {
    assert_eq!(TOTAL, 10);
    assert_eq!(selected(&[11, 13, 17]), 28);
}
