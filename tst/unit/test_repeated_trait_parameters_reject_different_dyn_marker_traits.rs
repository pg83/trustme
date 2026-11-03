//@ compile-fail: Same
use core::marker::PhantomData;
struct Pair<A, B>(PhantomData<(A, B)>);
trait Same { fn code() -> u16; }
impl<T: ?Sized> Same for Pair<*const T, *const T> { fn code() -> u16 { 389 } }
fn main() {
    let _ = <Pair<*const (dyn Iterator<Item = u16> + Send), *const (dyn Iterator<Item = u16> + Sync)> as Same>::code();
}
