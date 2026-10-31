#![feature(type_ascription)]
fn main() {
    let mut output = [0u8; 2];
    [type_ascribe!(output[0], u8), .., output[1]] = [17, 23, 31, 37];
    assert_eq!(output, [17, 37]);
}
