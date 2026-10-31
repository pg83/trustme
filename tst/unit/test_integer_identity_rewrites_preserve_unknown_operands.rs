#[inline(never)]
fn calculate(value: u32) -> [u32; 8] {
    [value + 0, value - 0, value * 1, 1 * value, value * 0, 0 * value, value / 1, value % 1]
}
fn main() {
    let value = std::hint::black_box(37u32);
    assert_eq!(calculate(value), [37, 37, 37, 37, 0, 0, 37, 0]);
}
