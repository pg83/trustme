// `_` in const-argument position is an inference placeholder, not an
// expression: `[0; _]` takes its length from the expected type. Lowering used
// to reject it with "`_` is only valid in expressions on the left-hand side of
// an assignment". Parentheses around it are dropped by the parser, so `(_)`
// must behave the same.
//
// Same shapes as rustc's check-pass ui tests
// const-generics/generic_arg_infer/array-repeat-expr-lib.rs and
// const-generics/generic_arg_infer/parend_infer.rs.
fn deferred() -> usize {
    let s: [u8; 10];
    s = [0; _];
    s.len()
}

fn main() {
    assert_eq!(deferred(), 10);

    // The size comes from the annotation on the binding.
    let direct: [u8; 4] = [7; _];
    assert_eq!(direct.len(), 4);
    assert_eq!(direct[3], 7);

    // Parenthesised, in both the type and the expression.
    #[rustfmt::skip]
    let parens: [u8; (_)] = [1; (((_)))];
    let sized: [u8; 2] = parens;
    assert_eq!(sized, [1, 1]);
}
