struct Route<A, B, const X: usize, const Y: usize>(A, B);

impl<A, B> Route<A, B, 1, 2> {
    fn value(&self) -> u8 {
        12
    }
}

impl<T, const N: usize> Route<T, T, N, N> {
    fn value(&self) -> u8 {
        33
    }
}

fn main() {
    let route = Route::<u8, u8, 1, 2>(4, 5);
    assert_eq!(route.value(), 12);
}
