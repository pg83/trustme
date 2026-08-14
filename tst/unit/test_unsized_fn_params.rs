#![feature(unsized_fn_params)]
#![allow(incomplete_features)]

use std::fmt::Display;

fn str_len(value: str) -> usize {
    value.len()
}

fn slice_sum(value: [u8]) -> u8 {
    value[0] + value[2]
}

fn display_len(value: dyn Display) -> usize {
    value.to_string().len()
}

fn main() {
    let text = String::from("rust").into_boxed_str();
    assert_eq!(str_len(*text), 4);

    let bytes: Box<[u8]> = Box::new([3, 4, 5]);
    assert_eq!(slice_sum(*bytes), 8);

    let display: Box<dyn Display> = Box::new(1234_u32);
    assert_eq!(display_len(*display), 4);
}
