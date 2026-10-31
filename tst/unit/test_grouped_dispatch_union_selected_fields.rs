union Word { number: u32, bytes: [u8; 4] }
fn classify_number(word: Word) -> u8 {
    unsafe { match word { Word { number: 37 } => 1, Word { number: 53 } => 2, _ => 0 } }
}
fn classify_bytes(word: Word) -> u8 {
    unsafe { match word { Word { bytes: [1, 2, 3, 4] } => 1, Word { bytes: [5, 6, _, 8] } => 2, _ => 0 } }
}
fn main() {
    assert_eq!(classify_number(Word { number: 37 }), 1);
    assert_eq!(classify_number(Word { number: 53 }), 2);
    assert_eq!(classify_number(Word { number: 71 }), 0);
    assert_eq!(classify_bytes(Word { bytes: [1, 2, 3, 4] }), 1);
    assert_eq!(classify_bytes(Word { bytes: [5, 6, 7, 8] }), 2);
    assert_eq!(classify_bytes(Word { bytes: [1, 2, 0, 4] }), 0);
}
