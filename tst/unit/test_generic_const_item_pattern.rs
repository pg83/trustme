#![feature(generic_const_items)]
#![allow(incomplete_features)]

const IDENTITY<const VALUE: u64>: u64 = VALUE;

fn main() {
    let result = match 2 {
        IDENTITY::<1> => 10,
        IDENTITY::<{ 1 + 1 }> => 20,
        _ => 0,
    };
    assert_eq!(result, 20);
}
