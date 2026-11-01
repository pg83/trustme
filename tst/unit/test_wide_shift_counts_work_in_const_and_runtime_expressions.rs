const LOW: u128 = (1u128 << 63u128) - 1;
const HIGH: u128 = 1u128 << 127u128;
const MIDDLE: u128 = (HIGH >> 63u128) | (LOW << 64u128);
const SIGN: i128 = (-1i128) << 127u128;

#[inline(never)]
fn shift(value: u128, bits: u128) -> (u128, u128) {
    (value << bits, value >> bits)
}

#[inline(never)]
fn arithmetic(value: i128, bits: u128) -> i128 {
    value >> bits
}

fn main() {
    assert_eq!(LOW, 0x7fff_ffff_ffff_ffff);
    assert_eq!(HIGH, 0x8000_0000_0000_0000_0000_0000_0000_0000);
    assert_eq!(MIDDLE, 0x7fff_ffff_ffff_ffff_0000_0000_0000_0000);
    assert_eq!(SIGN, i128::MIN);
    assert_eq!(shift(1, 0), (1, 1));
    assert_eq!(shift(1, 63), (1u128 << 63, 0));
    assert_eq!(shift(1, 64), (1u128 << 64, 0));
    assert_eq!(shift(1, 127), (HIGH, 0));
    assert_eq!(shift(HIGH | 3, 64), (3u128 << 64, 1u128 << 63));
    assert_eq!(arithmetic(SIGN, 127), -1);
    assert_eq!(arithmetic(-9, 0), -9);
    assert_eq!(arithmetic(-9, 64), -1);
}
