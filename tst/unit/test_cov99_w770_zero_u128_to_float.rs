const ZERO_FROM_U128: f64 = 0u128 as f64;

fn main() {
    assert_eq!(ZERO_FROM_U128.to_bits(), 0);
}
