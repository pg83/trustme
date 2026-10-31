union Word { wide: u32, bytes: [u8; 4] }
#[inline(never)]
fn number(value: Word) -> u32 { unsafe { match value { Word { wide } => wide } } }
#[inline(never)]
fn bytes(value: Word) -> u8 {
    unsafe { match value { Word { bytes: [first, second, third, last] } => first + second + third + last } }
}
fn main() {
    assert_eq!(number(Word { wide: 0x13572468 }), 0x13572468);
    assert_eq!(bytes(Word { bytes: [3, 5, 7, 11] }), 26);
}
