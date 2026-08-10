fn leading(values: [u8; 3]) -> u8 {
    match values {
        [1, 2, 3, ..] => 1,
        [1, 2, ..] => 2,
        _ => 0,
    }
}

fn trailing(values: [u8; 3]) -> u8 {
    match values {
        [.., 1, 2, 3] => 1,
        [.., 2, 3] => 2,
        _ => 0,
    }
}

fn split(values: [u8; 5]) -> (u8, usize, u8) {
    match values {
        [first, middle @ .., last] => (first, middle.len(), last),
    }
}

fn main() {
    assert_eq!(leading([1, 2, 3]), 1);
    assert_eq!(leading([1, 2, 4]), 2);
    assert_eq!(leading([0, 2, 3]), 0);

    assert_eq!(trailing([1, 2, 3]), 1);
    assert_eq!(trailing([0, 2, 3]), 2);
    assert_eq!(trailing([1, 2, 4]), 0);

    assert_eq!(split([4, 5, 6, 7, 8]), (4, 3, 8));
}
