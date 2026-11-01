struct Trio<T>(T, u16, u16);

fn select(value: Trio<u16>) -> u16 {
    match value {
        Trio(3, middle, 5) => middle,
        Trio { 0: 7, 2: last, .. } => last,
        whole @ Trio { .. } => whole.0 + whole.1,
    }
}

fn wildcard(value: Trio<u16>) -> u16 {
    match value { whole @ _ => whole.2 }
}

fn main() {
    assert_eq!(select(Trio(3, 11, 5)), 11);
    assert_eq!(select(Trio(7, 13, 17)), 17);
    assert_eq!(select(Trio(19, 23, 29)), 42);
    assert_eq!(wildcard(Trio(31, 37, 41)), 41);
}
