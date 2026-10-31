//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
use proc_macro_item_passthrough::token_texts;
fn main() {
    let symbols = token_texts!(<- / %= /= &= |= != <= && || << ? ~ ^ ^=);
    assert_eq!(symbols, "< - / % = / = & = | = ! = < = & & | | < < ? ~ ^ ^ =");
    assert_eq!(token_texts!('x' '\n'), "'x' '\\n'");
}
