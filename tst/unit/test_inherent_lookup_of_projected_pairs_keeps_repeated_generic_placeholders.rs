struct Pair<A, B>(A, B);
struct Source;
trait Project<T> {
    type Out;
}
impl<A, B> Project<(A, B)> for Source {
    type Out = Pair<A, B>;
}
impl<T> Pair<T, T> {
    fn tag() -> u8 { 73 }
}
fn left<T>() -> u8 {
    <Source as Project<(_, T)>>::Out::tag()
}
fn right<T>() -> u8 {
    <Source as Project<(T, _)>>::Out::tag()
}
fn main() {
    assert_eq!(left::<u16>(), 73);
    assert_eq!(right::<u32>(), 73);
}
