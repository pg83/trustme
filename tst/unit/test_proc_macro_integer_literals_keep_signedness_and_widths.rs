//@ proc-macro-aux-build: numeric_token_bridge.rs
use numeric_token_bridge::echo;
fn main() {
    let values = echo!((7u8, 0xab_cdu16, 70001u32, 1_000_000_001u64, 19usize));
    assert_eq!(values, (7u8, 43981u16, 70001u32, 1_000_000_001u64, 19usize));
    let signed = echo!((-7i8, -301i16, -70001i32, -1_000_000_001i64, -19isize));
    assert_eq!(signed, (-7i8, -301i16, -70001i32, -1_000_000_001i64, -19isize));
    assert_eq!(echo!(340282366920938463463374607431768211455u128), u128::MAX);
    assert_eq!(echo!(-170141183460469231731687303715884105728i128), i128::MIN);
    let inferred: u128 = echo!(18446744073709551616);
    assert_eq!(inferred, 1u128 << 64);
}
