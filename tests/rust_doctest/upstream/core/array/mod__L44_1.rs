// Extracted from library/core/src/array/mod.rs:44
#![allow(unused)]
#![feature(array_repeat)]
fn main() {

    use std::array;

    let string = "Hello there!".to_string();
    let strings = array::repeat(string);
    assert_eq!(strings, ["Hello there!", "Hello there!"]);
}
