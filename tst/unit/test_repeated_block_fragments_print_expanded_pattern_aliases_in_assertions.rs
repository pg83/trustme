//@ compile-variant: -O
#![feature(pattern_types, pattern_type_macro)]
macro_rules! twice { ($body:expr) => {{ let first = $body; assert!($body); first }}; }
fn main() {
    assert!(twice!({
        type Small = std::pat::pattern_type!(u16 is 3..=9);
        #[derive(Clone, PartialEq, Eq)]
        struct Local<T> { value: T }
        let value: Small = 5;
        let plain = unsafe { std::mem::transmute::<Small, u16>(value) };
        plain == 5 && Local { value: 7u8 }.clone() == Local { value: 7 }
    }));
}
