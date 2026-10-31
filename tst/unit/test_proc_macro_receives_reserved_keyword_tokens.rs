//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
use proc_macro_item_passthrough::token_texts;
fn main() {
    assert_eq!(token_texts!(priv static crate mod struct enum trait use impl type where),
               "priv static crate mod struct enum trait use impl type where");
    assert_eq!(token_texts!(loop while do continue break super move abstract final override virtual typeof become unsized macro),
               "loop while do continue break super move abstract final override virtual typeof become unsized macro");
    assert_eq!(token_texts!(async await try), "async await try");
}
