//@ compile-variant: -O
trait Base {}
trait Select<T: ?Sized> { const VALUE: u8; }
impl Select<dyn Base + Send> for () { const VALUE: u8 = 17; }
impl Select<dyn Base + Sync> for () { const VALUE: u8 = 23; }
fn selected<T: ?Sized>() -> u8 where (): Select<T> { <() as Select<T>>::VALUE }
fn main() {
    assert_eq!(selected::<dyn Base + Send>(), 17);
    assert_eq!(selected::<dyn Base + Sync>(), 23);
}
