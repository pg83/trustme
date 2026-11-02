//@ proc-macro-aux-build: inspect_type_expansion_order.rs
//@ compile-variant: -O
#[derive(Clone, inspect_type_expansion_order::InspectTypes)]
struct Generic<T> { next: *const Self, value: T, callback: fn() }
fn main() {
    assert!(OBSERVED_SELF);
    assert!(!OBSERVED_PATTERN);
    let value = Generic { next: std::ptr::null(), value: 7u8, callback: || {} };
    let copied = value.clone();
    assert_eq!(copied.value, 7);
    assert!(copied.next.is_null());
    (copied.callback)();
}
