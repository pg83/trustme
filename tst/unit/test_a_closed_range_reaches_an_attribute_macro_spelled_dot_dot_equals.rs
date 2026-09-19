// A closed range is spelled `..=`; `...` was its spelling before Rust 1.26 and is not an
// expression at all any more. An attribute macro is handed the annotated item as tokens,
// so the spelling the compiler prints is the spelling the macro parses, and upstream's
// printer has only the one form: `RangeLimits::Closed => self.word("..=")` in
// `print_expr_inner` (rustc_ast_pretty/src/pprust/state/expr.rs). We printed the obsolete
// `...`, which our own parser still accepts but `syn` does not - rstest_reuse's
// `#[apply(..)]` over base64's engine tests failed with `expected `..=``.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item_spelled;

#[echo_item_spelled("..=")]
fn sum_of_a_closed_range() -> u32 {
    let mut total = 0;
    for value in 0_u32..=3 {
        total += value;
    }
    total
}

fn main() {
    assert_eq!(sum_of_a_closed_range(), 6);
}
