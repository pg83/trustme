//@ compile-variant: -O
trait Select<T: ?Sized> { const VALUE: u16; }
impl Select<dyn Send> for () { const VALUE: u16 = 17; }
impl Select<dyn Sync> for () { const VALUE: u16 = 23; }
fn selected<T: ?Sized>() -> u16 where (): Select<T> { <() as Select<T>>::VALUE }

fn main() {
    assert_eq!(selected::<dyn Send>(), 17);
    assert_eq!(selected::<dyn Sync>(), 23);
}
