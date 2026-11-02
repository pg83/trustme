//@ compile-variant: -O
trait Family {
    type First where Self: Sized;
    type Second where Self: Sized;
}
trait Select<T: ?Sized> { const VALUE: u8; }
impl Select<dyn Family<First = u16>> for () { const VALUE: u8 = 17; }
impl Select<dyn Family<Second = u16>> for () { const VALUE: u8 = 23; }
fn selected<T: ?Sized>() -> u8 where (): Select<T> { <() as Select<T>>::VALUE }
fn main() {
    assert_eq!(selected::<dyn Family<First = u16>>(), 17);
    assert_eq!(selected::<dyn Family<Second = u16>>(), 23);
}
