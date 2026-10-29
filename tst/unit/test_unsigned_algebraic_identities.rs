#[inline(never)]
fn identities(value: u64) -> [u64; 8] {
    [value + 0, value - 0, value * 1, 1 * value, value * 0, 0 * value, value / 1, value % 1]
}
fn main() {
    for value in [0, 7, u64::MAX] {
        assert_eq!(identities(value), [value, value, value, value, 0, 0, value, 0]);
    }
}
