#![feature(unsized_fn_params)]
fn consume(mut values: [u8]) -> u32 {
    assert_eq!(values.len(), 4);
    values[1] += values[0];
    values.iter().map(|n| u32::from(*n)).sum()
}
fn main() {
    let values: Box<[u8]> = vec![13, 17, 19, 23].into_boxed_slice();
    assert_eq!(consume(*values), 85);
}
