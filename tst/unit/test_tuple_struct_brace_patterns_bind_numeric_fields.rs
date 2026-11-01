struct Pair<T>(T, u16, T);
fn main() {
    let Pair { 0: first, 2: last, .. } = Pair(13u8, 99, 31u8);
    assert_eq!((first, last), (13, 31));
    let value = Pair(Some(17u8), 123, None);
    match value {
        Pair { 0: Some(first), 1: middle, 2: None } => assert_eq!((first, middle), (17, 123)),
        _ => panic!("wrong numeric-field pattern"),
    }
}
