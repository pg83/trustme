const fn split(value: u128, divisor: u128) -> (u128, u128) {
    (value / divisor, value % divisor)
}
const VALUE: u128 = (1u128 << 91) + 7;
const DIVISOR: u128 = (1u128 << 95) + 19;
const LARGE: (u128, u128) = split(VALUE, DIVISOR);
const SMALL: (u128, u128) = split(11, DIVISOR);
const SIGNED: (i128, i128) = (-(VALUE as i128) / (DIVISOR as i128), -(VALUE as i128) % (DIVISOR as i128));
fn main() {
    assert_eq!(LARGE, (0, VALUE));
    assert_eq!(SMALL, (0, 11));
    assert_eq!(SIGNED, (0, -(VALUE as i128)));
}
