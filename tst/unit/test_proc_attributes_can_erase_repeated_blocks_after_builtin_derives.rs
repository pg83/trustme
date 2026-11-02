//@ proc-macro-aux-build: inspect_type_expansion_order.rs
//@ compile-variant: -O
macro_rules! twice {
    ($body:expr) => {{
        let first = $body;
        let second = {
            #[inspect_type_expansion_order::erase_nested_block]
            fn check() -> bool { $body }
            check()
        };
        first && second
    }};
}
fn main() {
    assert!(twice!({
        #[derive(Clone, PartialEq, Eq)]
        struct Local<T> { value: T }
        Local { value: 11u16 }.clone() == Local { value: 11 }
    }));
}
