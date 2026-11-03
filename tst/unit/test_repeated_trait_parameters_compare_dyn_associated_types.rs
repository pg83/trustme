use core::marker::PhantomData;
struct Pair<A, B>(PhantomData<(A, B)>);
trait Same { fn code() -> u16; }
impl<T: ?Sized> Same for Pair<*const T, *const T> { fn code() -> u16 { 379 } }
trait Stream { type Item; type Error; }
fn inspect<T, E>() -> u16 {
    <Pair<*const dyn Stream<Item = T, Error = E>, *const dyn Stream<Error = E, Item = T>> as Same>::code()
}
fn main() {
    assert_eq!(inspect::<u32, u8>(), 379);
    assert_eq!(inspect::<[u16; 3], ()>(), 379);
    assert_eq!(<Pair<*const (dyn Iterator<Item = u32> + Send + Sync), *const (dyn Iterator<Item = u32> + Send + Sync)> as Same>::code(), 379);
}
