//@ compile-variant: -O
#![feature(arbitrary_self_types)]
trait Handle<const M: usize> { type Pointer; }
trait Read {
    fn read<T, const M: usize>(self: Box<Self>, token: T) -> (T, usize);
}
struct Value<const N: usize>([u8; N]);
impl<const N: usize, const M: usize> Handle<M> for Value<N> {
    type Pointer = Box<Self>;
}
impl<const N: usize> Read for Value<N> {
    fn read<T, const M: usize>(self: <Self as Handle<M>>::Pointer, token: T) -> (T, usize) {
        (token, self.0.len() + M)
    }
}
fn main() {
    assert_eq!(<Value<3> as Read>::read::<u16, 5>(Box::new(Value([1; 3])), 97), (97, 8));
    assert_eq!(<Value<7> as Read>::read::<bool, 2>(Box::new(Value([3; 7])), true), (true, 9));
}
