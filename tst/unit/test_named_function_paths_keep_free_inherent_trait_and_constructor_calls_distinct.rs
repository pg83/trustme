fn plain<T>(value: T) -> T { value }
struct Offset<const N: u8>;
impl<const N: u8> Offset<N> {
    fn inherent(value: u8) -> u8 { value + N }
}
trait Transform { fn transform(value: u8) -> u8; }
impl<const N: u8> Transform for Offset<N> {
    fn transform(value: u8) -> u8 { value * N }
}
struct Wrapped(u8);
enum Entry { Value(u8) }
fn apply<R, F: Fn(u8) -> R>(function: F, value: u8) -> R { function(value) }
fn selected<const N: u8>() -> impl Fn(u8) -> u8 { Offset::<N>::inherent }
fn main() {
    assert_eq!(apply(plain::<u8>, 4), 4);
    assert_eq!(apply(Offset::<3>::inherent, 4), 7);
    assert_eq!(apply(<Offset<3> as Transform>::transform, 4), 12);
    assert_eq!(apply(selected::<5>(), 4), 9);
    let free: fn(u8) -> u8 = plain::<u8>;
    let inherent: fn(u8) -> u8 = Offset::<3>::inherent;
    let method: fn(u8) -> u8 = <Offset<3> as Transform>::transform;
    assert_eq!((free(5), inherent(5), method(5)), (5, 8, 15));
    let Wrapped(value) = apply(Wrapped, 11);
    assert_eq!(value, 11);
    let Entry::Value(value) = apply(Entry::Value, 13);
    assert_eq!(value, 13);
    let callback = (|value: u8| value + 1) as fn(u8) -> u8;
    assert_eq!(apply(callback, 4), 5);
}
