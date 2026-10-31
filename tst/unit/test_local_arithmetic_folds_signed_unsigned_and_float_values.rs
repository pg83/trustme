//@ compile-flags: -Coverflow-checks=off
fn signed() -> [i16; 5] {
    let left = -37i16;
    let right = 5i16;
    [left + right, left - right, left * right, left / right, left % right]
}
fn unsigned() -> [u32; 5] {
    let left = 239u32;
    let right = 17u32;
    [left + right, left - right, left * right, left / right, left % right]
}
fn floats() -> [f64; 4] {
    let left = 3.5f64;
    let right = 0.5f64;
    [left + right, left - right, left * right, left / right]
}
fn main() {
    assert_eq!(signed(), [-32, -42, -185, -7, -2]);
    assert_eq!(unsigned(), [256, 222, 4063, 14, 1]);
    assert_eq!(floats(), [4.0, 3.0, 1.75, 7.0]);
}
