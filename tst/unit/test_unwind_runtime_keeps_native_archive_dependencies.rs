//@ native-static-aux-build: orchard_native_ops.rs
//@ aux-build: orchard_native_front.rs
fn main() {
    let mut values = [3u32, 5, 7];
    let returned = values.iter_mut().map(|value| orchard_native_front::update(value, 2)).collect::<Vec<_>>();
    assert_eq!(returned, [24, 40, 56]);
    assert_eq!(values, [5, 7, 9]);
}
