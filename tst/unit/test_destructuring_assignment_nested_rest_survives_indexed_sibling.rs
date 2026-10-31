struct Packet { first: u8, rest: [u8; 3] }
fn main() {
    let mut output = [0u8; 3];
    Packet { first: output[0], rest: [output[1], .., output[2]] } = Packet { first: 17, rest: [23, 31, 37] };
    assert_eq!(output, [17, 23, 37]);
}
