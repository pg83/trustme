use std::any::TypeId;
use std::mem::{align_of, size_of};

type Pinned = fn(&'static ());
type Any = for<'a> fn(&'a ());

#[repr(C)]
struct Record<F> {
    prefix: u8,
    invoke: F,
    suffix: u16,
}

fn main() {
    assert_ne!(TypeId::of::<Record<Pinned>>(), TypeId::of::<Record<Any>>());
    assert_eq!(size_of::<Record<Pinned>>(), size_of::<Record<Any>>());
    assert_eq!(align_of::<Record<Pinned>>(), align_of::<Record<Any>>());
}
