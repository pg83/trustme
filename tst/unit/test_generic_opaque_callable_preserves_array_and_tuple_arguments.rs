fn make<T, const N: usize>() -> impl Fn(([T; N], T)) -> ([T; N], T) {
    |value| value
}

fn main() {
    let call = make::<u16, 3>();
    assert_eq!(call(([257, 263, 269], 271)), ([257u16, 263, 269], 271));
    let empty = make::<String, 0>();
    let (_, result) = empty(([], String::from("carried")));
    assert_eq!(result, "carried");
}
