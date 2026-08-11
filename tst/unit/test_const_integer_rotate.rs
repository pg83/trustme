const U8_ZERO: u8 = 0x81u8.rotate_left(0);
const U8_WIDTH: u8 = 0x81u8.rotate_right(8);
const U8_OVER_WIDTH: u8 = 0x81u8.rotate_left(9);
const I8_OVER_WIDTH: i8 = (-2i8).rotate_right(9);
const U16_LARGE: u16 = 1u16.rotate_left(124);
const U32_LEFT: u32 = 0x0100_00b3u32.rotate_left(8);
const U64_LEFT: u64 = 0x0123_4567_89ab_cdefu64.rotate_left(8);
const U128_OVER_WIDTH: u128 = 1u128.rotate_left(132);

fn main() {
    assert_eq!(U8_ZERO, 0x81);
    assert_eq!(U8_WIDTH, 0x81);
    assert_eq!(U8_OVER_WIDTH, 0x03);
    assert_eq!(I8_OVER_WIDTH, 0x7f);
    assert_eq!(U16_LARGE, 0x1000);
    assert_eq!(U32_LEFT, 0x0000_b301);
    assert_eq!(U64_LEFT, 0x2345_6789_abcd_ef01);
    assert_eq!(U128_OVER_WIDTH, 0x10);

    let zero = std::hint::black_box(0);
    let width = std::hint::black_box(64);
    let over_width = std::hint::black_box(132);
    assert_eq!(std::hint::black_box(0x81u8).rotate_left(zero), 0x81);
    assert_eq!(std::hint::black_box(0x0123_4567_89ab_cdefu64).rotate_right(width),
               0x0123_4567_89ab_cdef);
    assert_eq!(std::hint::black_box(0x81u8).rotate_left(over_width), 0x18);
    assert_eq!(std::hint::black_box(0x0123_4567_89ab_cdefu64).rotate_left(over_width),
               0x1234_5678_9abc_def0);
    assert_eq!(std::hint::black_box(1u128).rotate_right(over_width), 1u128 << 124);
}
