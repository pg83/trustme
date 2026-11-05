//@ compile-fail: statically known size
#![feature(unsized_locals)]
fn main() {
    let boxed: Box<[u8]> = vec![13, 17, 19, 23].into_boxed_slice();
    let mut values: [u8] = *boxed;
    assert_eq!(values.len(), 4);
    assert_eq!(&values, &[13, 17, 19, 23]);
    values[2] += values[0];
    assert_eq!(&values, &[13, 17, 32, 23]);
}
