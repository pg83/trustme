trait Factory<T: Copy> {
    fn sequence(value: T) -> impl Iterator<Item = T> + Clone { std::iter::once(value) }
}
struct Source;
impl<T: Copy> Factory<T> for Source {}
fn join<T: Iterator<Item = u8>>(first: T, second: T) -> Vec<u8> {
    first.chain(second).collect()
}
fn main() {
    assert_eq!(join(Source::sequence(23u8), Source::sequence(29)), vec![23, 29]);
}
