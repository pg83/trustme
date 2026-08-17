// An integer literal's type comes from the operator around it. A shift of a
// literal yields the literal's own type, so an enclosing operator reaches it;
// and a borrowed primitive goes through the standard library's forwarding
// impls, which take the value type on the right and yield it.
fn egg_count(display_value: u32) -> usize {
    (0..32).filter(|i| display_value & (1 << i) != 0).count()
}

fn main() {
    assert_eq!(egg_count(0b1011), 3);

    let array = [0x42u8; 4];
    for b in &array {
        let lo = b & 0xf;
        let hi = (b >> 4) & 0xf;
        assert_eq!((lo, hi), (2u8, 4u8));
    }

    // The right side may itself be a reference, which the same impls accept.
    let left = &7u32;
    let right = &3u32;
    assert_eq!(left & right, 3u32);
    assert_eq!(left + 1, 8u32);
    assert!(left > right);
}
