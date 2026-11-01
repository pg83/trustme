//@ compile-variant: -O
struct Item<T>(T, u16);
#[inline(never)]
fn invoke<F: FnOnce(u16) -> Item<u16>>(call: F, value: u16) -> u16 {
    let Item(value, tag) = call(value);
    value + tag
}
fn make(value: u16) -> Item<u16> { Item(value * 3, 7) }
fn main() {
    assert_eq!(invoke(make, 11), 40);
    assert_eq!(invoke(|value| Item(value + 5, 13), 17), 35);
}
