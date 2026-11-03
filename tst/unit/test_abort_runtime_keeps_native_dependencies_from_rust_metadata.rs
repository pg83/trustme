//@ native-static-aux-build: orchard_native_ops.rs
//@ aux-build: orchard_native_front.rs
//@ compile-flags: -Cpanic=abort
fn main() {
    let mut value = 211u32;
    assert_eq!(orchard_native_front::update(&mut value, 13), 1688);
    assert_eq!(value, 224);
    assert_eq!(orchard_native_front::update(&mut value, u32::MAX), 1792);
    assert_eq!(value, 223);
}
