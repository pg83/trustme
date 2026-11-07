const fn shifted(mut value: i128, left: u32, right: u32) -> (i128, i128) {
    value <<= left;
    let high = value;
    value >>= right;
    (high, value)
}

const NEGATIVE: (i128, i128) = shifted(-(1i128 << 95) + 0x12345, 17, 83);
const POSITIVE: (i128, i128) = shifted((1i128 << 72) + 0xabcdef, 9, 67);

fn main() {
    assert_eq!(NEGATIVE, (-0xfffffffffffffffffffdb9760000i128, -536870912));
    assert_eq!(POSITIVE, (0x2000000000001579bde00i128, 16384));
    let input = std::hint::black_box(-129i128);
    assert_eq!(shifted(input, 7, 6), (-16512, -258));
}
