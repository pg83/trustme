#![feature(no_core)]
#![no_core]

fn ordinary(value: i32) -> i32 {
    value
}

pub fn copies(value: i32) -> i32 {
    let first = value;
    let second = first;
    second
}

pub fn aggregate(first: i32, second: i32) -> i32 {
    let pair = (first, second);
    let selected = pair.0;
    selected
}

pub fn ordinary_call(value: i32) -> i32 {
    ordinary(value)
}
