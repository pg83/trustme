const fn compute(mut value: u128, factor: u128) -> (u128, u128) {
    value *= factor;
    let product = value;
    value &= (1u128 << 100) - 1;
    (product, value)
}

const RESULT: (u128, u128) = compute(0x80000000000000012345, 0x200000101);

fn main() {
    assert_eq!(RESULT.0, 0x10000008080000002468a01246845);
    assert_eq!(RESULT.1, 0x8080000002468a01246845);
    assert_eq!(compute(17, 19), (323, 323));
}
