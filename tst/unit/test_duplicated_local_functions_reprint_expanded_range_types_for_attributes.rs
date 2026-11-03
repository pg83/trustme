//@ proc-macro-aux-build: erase_reprinted_local_functions.rs
//@ compile-variant: -O
#![feature(pattern_types, pattern_type_macro, proc_macro_hygiene, stmt_expr_attributes)]
macro_rules! evaluate_then_erase {
    ($body:expr) => {{
        let first = $body;
        let second = #[erase_reprinted_local_functions::erase] $body;
        (first, second)
    }};
}
fn main() {
    let pair: (u8, u8) = evaluate_then_erase!({
        fn selected() -> u8 {
            let bounded: std::pat::pattern_type!(u8 is 3..=9) = 7;
            unsafe { std::mem::transmute(bounded) }
        }
        selected()
    });
    assert_eq!(pair, (7, 0));
}
