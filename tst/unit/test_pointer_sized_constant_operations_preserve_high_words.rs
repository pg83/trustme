const HIGH: usize = 0x173b_2547_91ab_cdef;
const SIGNED: isize = -0x173b_2547_91ab;
fn main() {
    let value = HIGH;
    let sum = value.wrapping_add(37);
    let shifted = value >> 32;
    let negative = SIGNED;
    assert_eq!((sum, shifted), (0x173b_2547_91ab_ce14, 0x173b_2547));
    assert_eq!((!negative, -negative), (0x173b_2547_91aa, 0x173b_2547_91ab));
    assert_eq!(u64::MAX as usize, usize::MAX);
    assert_eq!((u128::MAX as usize, i128::MIN as isize), (usize::MAX, 0));
}
