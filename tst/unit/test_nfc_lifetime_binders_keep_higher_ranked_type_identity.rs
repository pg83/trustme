use std::any::TypeId;
type Named = for<'λè̕> fn(&'λè̕ ());
type Plain = for<'a> fn(&'a ());
fn main() {
    assert_eq!(TypeId::of::<Named>(), TypeId::of::<Plain>());
    assert_ne!(TypeId::of::<Named>(), TypeId::of::<fn(&'static ())>());
}
