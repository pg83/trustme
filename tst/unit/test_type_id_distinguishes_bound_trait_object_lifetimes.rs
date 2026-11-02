//@ compile-variant: -O
use std::any::TypeId;
trait Family<'a> {}
fn main() {
    assert_eq!(TypeId::of::<dyn for<'a> Family<'a>>(),
               TypeId::of::<dyn for<'b> Family<'b>>());
    assert_ne!(TypeId::of::<dyn for<'a> Family<'a>>(),
               TypeId::of::<dyn Family<'static>>());
}
