//@ compile-flags: -Coverflow-checks=off
#[inline(never)]
fn identities(value: u32) -> [u32; 9] {
    [value + 0, value - 0, value / 1, value % 1,
     value * 0, value * 1, 0 * value, 1 * value, value * 3]
}
fn main() {
    assert_eq!(identities(37), [37, 37, 37, 0, 0, 37, 0, 37, 111]);
    assert_eq!(identities(0), [0; 9]);
}
