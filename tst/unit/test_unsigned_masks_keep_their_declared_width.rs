fn masks() -> (u8, i8, u128, i128) {
    let narrow_unsigned = 37u8;
    let narrow_signed = -37i8;
    let wide_unsigned = (1u128 << 100) | 42;
    let wide_signed = (1i128 << 100) | 42;
    (!narrow_unsigned, !narrow_signed, !wide_unsigned, !wide_signed)
}
fn remainder() -> u16 { let value = 239u16; let divisor = 17u16; value % divisor }
fn main() {
    assert_eq!(masks(), (218, 36, u128::MAX ^ ((1u128 << 100) | 42), -((1i128 << 100) | 42) - 1));
    assert_eq!(remainder(), 1);
}
