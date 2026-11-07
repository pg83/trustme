const SMALL: u8 = 243u8.wrapping_add(29);
const WORD: u64 = u64::MAX.wrapping_add(7);
const POINTER: usize = usize::MAX.wrapping_add(9);
const WIDE: u128 = ((1u128 << 100) | 0xa5).wrapping_mul(3);
const SIGNED: [i128; 3] = [-700i128 / 7, 700i128 / 7, -701i128 % 7];
const FLAG: bool = !false;

fn main() {
    assert_eq!((SMALL, WORD, POINTER), (16, 6, 8));
    assert_eq!(WIDE, 0x300000000000000000000001efu128);
    assert_eq!(SIGNED, [-100, 100, -1]);
    assert!(FLAG);
}
