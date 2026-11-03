use core::marker::PhantomData;
struct Slot<T>(PhantomData<T>);
impl<T: Copy> Slot<T> {
    fn accept(&self, value: T) -> T { value }
}
fn main() {
    let slot = Slot(PhantomData);
    assert_eq!(slot.accept(83u32), 83);
    let other = Slot(PhantomData);
    assert_eq!(other.accept([5u16, 7, 11]), [5, 7, 11]);
}
