struct Pair<A, B>(A, B);
struct Source;
trait Project<T> {
    type Out;
}
impl<A, B> Project<(A, B)> for Source {
    type Out = Pair<A, B>;
}
impl<T> Pair<(T, u8), (T, u8)> {
    fn tag() -> u8 { 29 }
}
fn select<T>() -> u8 {
    <Source as Project<((T, u8), (_, u8))>>::Out::tag()
}
fn main() {
    assert_eq!(select::<u16>(), 29);
    assert_eq!(select::<u32>(), 29);
}
