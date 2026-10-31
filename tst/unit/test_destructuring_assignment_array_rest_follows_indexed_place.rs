fn main() {
    let mut output = [0u8; 2];
    [output[0], .., output[1]] = [17, 23, 31, 37];
    assert_eq!(output, [17, 37]);
}
