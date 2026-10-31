extern { fn abs(value: i32) -> i32; }
fn main() { unsafe { assert_eq!(abs(-37), 37); } }
