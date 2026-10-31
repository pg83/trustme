fn main() {
    let mut output = [0u8; 2];
    let mut position = 0usize;
    (output[{ position = 1; position }], .., output[0]) = (17, 23, 31, 37);
    assert_eq!((output, position), ([37, 17], 1));
}
