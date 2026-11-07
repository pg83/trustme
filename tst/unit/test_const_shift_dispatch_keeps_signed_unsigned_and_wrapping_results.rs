const UNSIGNED: (u128, u128) = {
    let left = ((1u128 << 94) + 0x10203) << 9;
    (left, left >> 70)
};

const SIGNED: (i128, i128) = {
    let left = (-(1i128 << 91) + 0x25) << 11;
    (left, left >> 66)
};

const WRAPPED: ((u8, bool), (i8, bool)) = (
    0x91u8.overflowing_shl(10),
    (-65i8).overflowing_shr(9),
);

const NON_SHIFTS: (u128, i128, f64) = (
    (17u128 * 19 + 11) / 2,
    (-17i128 * 19 + 11) / 2,
    (1.5f64 + 0.25) * 8.0 - 3.0,
);

fn main() {
    assert_eq!(UNSIGNED, (0x80000000000000000002040600u128, 8589934592));
    assert_eq!(SIGNED, (-0x3ffffffffffffffffffffed800i128, -68719476736));
    assert_eq!(WRAPPED, ((0x44, true), (-33, true)));
    assert_eq!(NON_SHIFTS, (167, -156, 11.0));
    let input = std::hint::black_box(3u8);
    assert_eq!((input << 2, input >> 1), (12, 1));
}
