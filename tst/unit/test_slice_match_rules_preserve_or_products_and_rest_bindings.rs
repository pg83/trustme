fn select(value: &[u8]) -> u16 {
    match value {
        b"owl" => 3,
        [1 | 2, middle @ .., 7 | 9] => middle.iter().map(|v| *v as u16).sum(),
        [11, second] => *second as u16,
        [] => 13,
        _ => 17,
    }
}

fn ends(value: &[u8]) -> u16 {
    match value { [3, ..] => 19, [.., 5] => 23, _ => 29 }
}

fn main() {
    assert_eq!(select(b"owl"), 3);
    for (first, last) in [(1, 7), (1, 9), (2, 7), (2, 9)] {
        assert_eq!(select(&[first, 31, 37, last]), 68);
    }
    assert_eq!(select(&[1, 7]), 0);
    assert_eq!(select(&[11, 41]), 41);
    assert_eq!(select(&[]), 13);
    assert_eq!(select(&[43]), 17);
    assert_eq!(ends(&[3, 47]), 19);
    assert_eq!(ends(&[53, 5]), 23);
    assert_eq!(ends(&[59, 61]), 29);
}
