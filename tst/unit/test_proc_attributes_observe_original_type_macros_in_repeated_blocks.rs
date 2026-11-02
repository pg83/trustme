//@ proc-macro-aux-build: inspect_type_expansion_order.rs
//@ compile-fail: Failed to find an impl of
#![feature(pattern_types, pattern_type_macro)]
macro_rules! twice {
    ($body:expr) => {{
        let first = $body;
        let second = {
            #[inspect_type_expansion_order::observe_nested_pattern]
            fn check() -> bool { $body }
            check()
        };
        first && second
    }};
}
fn main() {
    assert!(twice!({
        type Small = std::pat::pattern_type!(u16 is 3..=9);
        #[derive(Clone, PartialEq)]
        struct Local<T> { value: T }
        let value: Small = 5;
        unsafe { std::mem::transmute::<Small, u16>(value) == 5 }
            && Local { value: 13u8 }.clone() == Local { value: 13 }
    }));
}
