fn fixed(tag: u8, bytes: &[u16]) -> u16 {
    match (tag, bytes) {
        (1 | 4, [7, 9]) => 17,
        (1 | 4, [7, 12]) => 19,
        _ => 0,
    }
}

fn split(tag: u8, bytes: &[u16]) -> usize {
    match (tag, bytes) {
        (2 | 5, [3, middle @ .., 11]) => 20 + middle.len(),
        _ => 0,
    }
}

fn main() {
    for tag in [1, 4] {
        assert_eq!(fixed(tag, &[7, 9]), 17);
        assert_eq!(fixed(tag, &[7, 12]), 19);
        assert_eq!(fixed(tag, &[7]), 0);
        assert_eq!(fixed(tag, &[8, 9]), 0);
    }
    for tag in [2, 5] {
        assert_eq!(split(tag, &[3, 7, 8, 11]), 22);
        assert_eq!(split(tag, &[3, 11]), 20);
        assert_eq!(split(tag, &[3]), 0);
        assert_eq!(split(tag, &[3, 7, 12]), 0);
    }
    assert_eq!(fixed(2, &[7, 9]), 0);
    assert_eq!(split(1, &[3, 11]), 0);
}
