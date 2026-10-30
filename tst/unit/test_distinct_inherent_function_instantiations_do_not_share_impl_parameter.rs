//@ compile-fail: Failed to find an impl
trait Twin {}
impl<F> Twin for (F,F) {}
struct Ops;
impl Ops { fn width<T>()->usize { std::mem::size_of::<T>() } }
fn require<T:Twin>(_:T) {}
fn main() { require((Ops::width::<u8>,Ops::width::<u16>)); }
