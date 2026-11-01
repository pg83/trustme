fn select(value: (u16, u16, u16, u16)) -> u16 {
    match value {
        (3, second, 5, fourth) => second + fourth,
        (7, .., last) => last,
        (.., 11, last) => last + 13,
        whole @ _ => whole.1,
    }
}

fn main() {
    assert_eq!(select((3, 17, 5, 19)), 36);
    assert_eq!(select((7, 23, 29, 31)), 31);
    assert_eq!(select((37, 41, 11, 43)), 56);
    assert_eq!(select((47, 53, 59, 61)), 53);
    let nested = ((67u16, 71u16), 73u16);
    assert_eq!(match nested { (pair, _) => pair.0 + pair.1 }, 138);
}
