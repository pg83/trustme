const WIDE: u128 = (1u128 << 120) | (173u128 << 64) | 0x5a;
const QUOTIENT: u128 = WIDE / 17;
const REMAINDER: u128 = WIDE % 17;
const MASKED: u128 = WIDE ^ (0xffu128 << 64);

const fn classify(value: i128) -> u8 {
    if value <= -7 {
        13
    } else if value > 7 {
        17
    } else if value >= 0 {
        19
    } else {
        23
    }
}

const NEGATIVE: u8 = classify(-(1i128 << 119));
const POSITIVE: u8 = classify(1i128 << 119);
const _: [(); 13] = [(); NEGATIVE as usize];
const _: [(); 17] = [(); POSITIVE as usize];

fn main() {
    assert_eq!(QUOTIENT, 0x000f0f0f0f0f0f193c3c3c3c3c3c3c41);
    assert_eq!(REMAINDER, 9);
    assert_eq!(MASKED, 0x0100000000000052000000000000005a);
    let value = std::hint::black_box(WIDE);
    assert_eq!(value / 17, QUOTIENT);
    assert_eq!(value % 17, REMAINDER);
    assert_eq!(classify(std::hint::black_box(-7)), 13);
    assert_eq!(classify(std::hint::black_box(-1)), 23);
    assert_eq!(classify(std::hint::black_box(0)), 19);
    assert_eq!(classify(std::hint::black_box(8)), 17);
}
