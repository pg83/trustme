#[inline(never)]
fn choose(first: bool, value: u8) -> u16 {
    if first {
        match value {
            1 => return 3,
            2 => return 5,
            3 => return 7,
            4 => return 11,
            _ => return 13,
        }
    } else {
        match value {
            11 => return 3,
            12 => return 5,
            13 => return 7,
            14 => return 11,
            _ => return 13,
        }
    }
}

fn main() {
    for (value, left, right) in [(1, 3, 13), (2, 5, 13), (3, 7, 13), (4, 11, 13), (11, 13, 3), (12, 13, 5), (13, 13, 7), (14, 13, 11), (99, 13, 13)] {
        assert_eq!(choose(true, value), left);
        assert_eq!(choose(false, value), right);
    }
}
