fn bytes(value: &[u8; 3]) -> u16 {
    match value {
        b"cat" => 3,
        [1, middle, 5] => *middle as u16,
        [7, .., last] => *last as u16,
        _ => 11,
    }
}

fn small(value: [u16; 4]) -> u16 {
    match value { [3, middle @ .., last] => middle[0] + middle[1] + last, _ => 13 }
}

fn large(value: [u16; 40]) -> u16 {
    match value {
        [3, rest @ .., 5] => rest.len() as u16 + 8,
        [7, ..] => 11,
        _ => 13,
    }
}

fn main() {
    assert_eq!(bytes(b"cat"), 3);
    assert_eq!(bytes(&[1, 17, 5]), 17);
    assert_eq!(bytes(&[7, 19, 23]), 23);
    assert_eq!(bytes(&[29, 31, 37]), 11);
    assert_eq!(small([3, 41, 43, 47]), 131);
    assert_eq!(small([53, 59, 61, 67]), 13);
    let mut value = [0u16; 40];
    value[0] = 3;
    value[39] = 5;
    assert_eq!(large(value), 46);
    value[0] = 7;
    assert_eq!(large(value), 11);
    value[0] = 17;
    assert_eq!(large(value), 13);
}
