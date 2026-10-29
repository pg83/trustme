fn repeated<T: Copy>(value: T, depth: u8) -> impl Iterator<Item = T> {
    if depth == 0 { return std::iter::once(value); }
    repeated(value, depth - 1)
}
trait Build {
    fn build<T: Copy>(&self, value: T) -> impl Iterator<Item = T>;
}
impl Build for () {
    fn build<T: Copy>(&self, value: T) -> impl Iterator<Item = T> {
        repeated(value, 2)
    }
}
fn main() {
    assert_eq!(repeated(7u16, 3).next(), Some(7));
    assert_eq!(().build(2.5f64).next(), Some(2.5));
    assert_eq!(().build(true).next(), Some(true));
}
