// std writes `Box<dyn Any + Send + 'static>`, futures and tokio-reactor
// write `Box<dyn Any + Send>`. Upstream compares types with their regions
// erased: `'static` and the default object bound are both free regions, so
// the two are one type with one `TypeId` and one drop glue. Only a region
// bound by a binder keeps its place (see test_type_id_higher_rank_lifetimes).
use std::any::{Any, TypeId};

fn written() -> Box<dyn Any + Send + 'static> {
    Box::new(1u8)
}

fn defaulted() -> Box<dyn Any + Send> {
    Box::new(2u8)
}

fn main() {
    assert_eq!(TypeId::of::<dyn Any + Send + 'static>(), TypeId::of::<dyn Any + Send>());
    let x = written();
    let y = defaulted();
    let both: [Box<dyn Any + Send>; 2] = [x, y];
    assert_eq!(both[0].downcast_ref::<u8>(), Some(&1));
    assert_eq!(both[1].downcast_ref::<u8>(), Some(&2));
}
