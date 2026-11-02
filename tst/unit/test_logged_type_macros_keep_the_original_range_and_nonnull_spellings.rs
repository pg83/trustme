//@ compile-variant: -O
#![feature(log_syntax, pattern_types, pattern_type_macro)]
macro_rules! inspect_type {
    ($ty:ty) => {{ log_syntax!($ty); stringify!($ty) }};
}
fn main() {
    let range = inspect_type!(std::pat::pattern_type!(i16 is ..=-4 | 4..));
    let present = inspect_type!(std::pat::pattern_type!(*const u16 is !null));
    assert!(range.contains("pattern_type"));
    assert!(present.contains("pattern_type"));
    assert!(range.contains('4'));
    assert!(present.contains("null"));
}
