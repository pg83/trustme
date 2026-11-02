//@ compile-variant: -O
trait Family {
    type Value;
    type Optional where Self: Sized;
}
trait Select<T: ?Sized> { const VALUE: u8; }
impl Select<dyn Family<Value = u8>> for () { const VALUE: u8 = 17; }
impl Select<dyn Family<Value = u8, Optional = u16>> for () { const VALUE: u8 = 23; }
fn selected<T: ?Sized>() -> u8 where (): Select<T> { <() as Select<T>>::VALUE }
fn main() {
    assert_eq!(selected::<dyn Family<Value = u8>>(), 17);
    assert_eq!(selected::<dyn Family<Value = u8, Optional = u16>>(), 23);
}
