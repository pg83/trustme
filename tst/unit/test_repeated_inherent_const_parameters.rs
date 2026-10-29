struct Count<const N: usize>;
struct Pair<A, B>(A, B);
impl<T> Pair<T, T> {
    fn width(&self) -> usize { std::mem::size_of::<T>() }
}
fn main() {
    let pair = Pair(Count::<3>, Count::<3>);
    assert_eq!(Pair::<Count<3>, Count<3>>::width(&pair), 0);
}
