// __trustme_revmap was missing the entry for nibble 13, corrupting runtime
// reverse_bits() of any value with a high nibble.
fn main() {
    for n in 0u16..=255 { assert_eq!(n.reverse_bits(), reference(n), "n={}", n); }
    assert_eq!((13u16).reverse_bits(), 0b1011_0000_0000_0000);
    assert_eq!((0xD0u8).reverse_bits(), 0x0B);
}
fn reference(n: u16) -> u16 { let mut r = 0u16; for i in 0..16 { if n & (1 << i) != 0 { r |= 1 << (15 - i); } } r }
