fn main() {
    let mut output = [0u8; 2];
    macro_rules! first { () => { output[0] }; }
    [first!(), .., output[1]] = [17, 23, 31];
    assert_eq!(output, [17, 31]);
}
