union Bits { number: u32, bytes: [u8; 4] }
unsafe fn classify(value: Bits, allow: bool) -> u8 {
    match value { Bits { number: 37 } if allow => 7, Bits { number: 38..=41 } => 11, _ => 13 }
}
fn main() {
    unsafe {
        assert_eq!(classify(Bits { number: 37 }, true), 7);
        assert_eq!(classify(Bits { number: 37 }, false), 13);
        assert_eq!(classify(Bits { number: 41 }, false), 11);
    }
}
