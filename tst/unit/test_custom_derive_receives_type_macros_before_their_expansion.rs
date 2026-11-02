//@ proc-macro-aux-build: inspect_type_expansion_order.rs
//@ compile-variant: -O
#![feature(pattern_types, pattern_type_macro)]
#[derive(Clone, Copy, inspect_type_expansion_order::InspectTypes)]
struct Shape { small: std::pat::pattern_type!(u16 is 3..=9), callback: fn() }
fn main() {
    assert!(OBSERVED_PATTERN);
    assert!(!OBSERVED_SELF);
    let value = Shape { small: 5, callback: || {} };
    let copied = value.clone();
    assert_eq!(unsafe { std::mem::transmute::<_, u16>(copied.small) }, 5);
    (copied.callback)();
}
