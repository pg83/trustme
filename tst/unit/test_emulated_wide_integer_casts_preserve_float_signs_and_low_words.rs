//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Zmir-opt-level=0
//@ input: emulated_i128_target.toml

#[inline(never)]
fn unsigned(value: u128) -> (u8, f32, f64) {
    (value as u8, value as f32, value as f64)
}

#[inline(never)]
fn signed(value: i128) -> (i16, f32, f64) {
    (value as i16, value as f32, value as f64)
}

fn main() {
    let positive = std::hint::black_box((1u128 << 80) + 19);
    let negative = std::hint::black_box(-((1i128 << 80) + 23));
    let (low, single, double) = unsigned(positive);
    assert_eq!(low, 19);
    assert_eq!(single, 1208925819614629174706176.0f32);
    assert_eq!(double, 1208925819614629174706176.0f64);
    let (low, single, double) = signed(negative);
    assert_eq!(low, -23);
    assert_eq!(single, -1208925819614629174706176.0f32);
    assert_eq!(double, -1208925819614629174706176.0f64);
}
