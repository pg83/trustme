#![feature(f16)]

const MIN_POSITIVE_LITERAL: f16 = 6.103515625e-5_f16;
const MAX_SUBNORMAL: f16 = f16::MIN_POSITIVE.next_down();

fn main() {
    assert_eq!(MIN_POSITIVE_LITERAL.to_bits(), 0x0400);
    assert_eq!(MAX_SUBNORMAL.to_bits(), 0x03ff);
}
