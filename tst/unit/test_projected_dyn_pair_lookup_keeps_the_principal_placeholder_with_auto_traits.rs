use std::marker::PhantomData;
struct Pair<A: ?Sized, B: ?Sized>(PhantomData<(*const A, *const B)>);
struct Source;
trait Data<T> {}
trait Project<A: ?Sized, B: ?Sized> { type Out; }
impl<A: ?Sized, B: ?Sized> Project<A, B> for Source {
    type Out = Pair<A, B>;
}
impl<T: ?Sized> Pair<T, T> {
    fn tag() -> u8 { 47 }
}
fn select<T>() -> u8 {
    <Source as Project<dyn Data<T> + Send, dyn Data<_> + Send>>::Out::tag()
}
fn main() {
    assert_eq!(select::<u16>(), 47);
    assert_eq!(select::<u32>(), 47);
}
