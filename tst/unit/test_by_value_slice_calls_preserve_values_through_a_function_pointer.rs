//@ compile-variant: -O
#![feature(unsized_fn_params)]
#![allow(incomplete_features)]

#[inline(never)]
fn weighted(values: [u16]) -> u16 {
    let mut result = 0;
    for (index, value) in values.iter().enumerate() {
        result += (index as u16 + 1) * value;
    }
    result
}
#[inline(never)]
fn dispatch(call: fn([u16]) -> u16, values: Box<[u16]>) -> u16 {
    call(*values) + 7
}

fn main() {
    let first: Box<[u16]> = Box::new([3, 5, 7]);
    assert_eq!(dispatch(weighted, first), 41);
    let second: Box<[u16]> = Box::new([11, 13]);
    assert_eq!(dispatch(weighted, second), 44);
}
