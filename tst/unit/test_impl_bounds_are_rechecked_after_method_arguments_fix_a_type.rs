//@ compile-fail: type annotations needed
use core::marker::PhantomData;
struct Slot<T>(PhantomData<T>);
impl<T: Copy> Slot<T> {
    fn accept(&self, value: T) -> T { value }
}
fn main() {
    let slot = Slot(PhantomData);
    let _ = slot.accept(vec![7u32, 11]);
}
