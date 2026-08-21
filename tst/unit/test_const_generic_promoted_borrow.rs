fn promoted<const N: usize>() -> &'static usize {
    &(3 + N)
}

fn main() {
    assert_eq!(promoted::<13>(), &16);
    assert_eq!(promoted::<21>(), &24);
}
