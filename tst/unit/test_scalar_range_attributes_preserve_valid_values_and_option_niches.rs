#![feature(rustc_attrs)]

#[repr(transparent)]
#[rustc_layout_scalar_valid_range_start(1)]
#[rustc_layout_scalar_valid_range_end(7)]
struct Bounded(u8);

fn value(input: Option<Bounded>) -> u8 {
    match input {
        Some(inner) => inner.0,
        None => 0,
    }
}

fn main() {
    assert_eq!(core::mem::size_of::<Option<Bounded>>(), 1);
    assert_eq!(value(None), 0);
    assert_eq!(value(Some(unsafe { Bounded(1) })), 1);
    assert_eq!(value(Some(unsafe { Bounded(7) })), 7);
}
