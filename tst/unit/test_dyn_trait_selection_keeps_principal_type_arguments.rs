//@ compile-variant: -O
trait Family<T> { type Value; }
trait Select<T: ?Sized> { const VALUE: u8; }
impl Select<dyn Family<u16, Value = u8>> for () { const VALUE: u8 = 17; }
impl Select<dyn Family<u32, Value = u8>> for () { const VALUE: u8 = 23; }
fn selected<T: ?Sized>() -> u8 where (): Select<T> { <() as Select<T>>::VALUE }
fn main() {
    assert_eq!(selected::<dyn Family<u16, Value = u8>>(), 17);
    assert_eq!(selected::<dyn Family<u32, Value = u8>>(), 23);
}
