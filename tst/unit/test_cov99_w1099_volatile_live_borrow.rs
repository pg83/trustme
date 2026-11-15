//@ edition: 2021
//@ compile-flags: -Cpanic=abort

fn main() {
    let value = 37u32;
    let slot = &value;
    let loaded = unsafe { std::ptr::read_volatile::<&u32>(&slot) };
    assert_eq!(*loaded, 37);
    assert!(std::ptr::eq(loaded, &value));
}
