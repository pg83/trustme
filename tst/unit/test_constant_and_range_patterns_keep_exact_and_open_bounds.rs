const PAIR: (i16, i16) = (3, 5);
const BYTES: &[u8] = b"fir";
#[inline(never)]
fn tuple(value: (i16, i16)) -> u8 { match value { PAIR => 7, _ => 11 } }
#[inline(never)]
fn number(value: i16) -> u8 {
    match value { ..=-5 => 3, -4..0 => 5, 0..=7 => 7, 8.. => 11 }
}
#[inline(never)]
fn bytes(value: &[u8]) -> bool { matches!(value, BYTES) }
fn main() {
    assert_eq!((tuple((3, 5)), tuple((3, 7))), (7, 11));
    assert_eq!((number(-9), number(-1), number(0), number(7), number(8)), (3, 5, 7, 7, 11));
    assert!(bytes(b"fir"));
    assert!(!bytes(b"elm"));
}
