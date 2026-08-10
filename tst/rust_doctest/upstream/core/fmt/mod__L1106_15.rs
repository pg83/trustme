// Extracted from library/core/src/fmt/mod.rs:1106
#![allow(unused)]
fn main() {
    let x = 42; // 42 is '101010' in binary

    assert_eq!(format!("{x:b}"), "101010");
    assert_eq!(format!("{x:#b}"), "0b101010");

    assert_eq!(format!("{:b}", -16), "11111111111111111111111111110000");
}
