macro_rules! keep { ($value:expr) => { $value }; }
fn main() {
    let mut value = 12u32;
    keep!(value += 8);
    keep!(value -= 3);
    keep!(value *= 2);
    keep!(value /= 5);
    keep!(value %= 3);
    keep!(value |= 8);
    keep!(value &= 10);
    keep!(value ^= 3);
    keep!(value <<= 2);
    keep!(value >>= 1);
    assert_eq!(value, 22);
}
