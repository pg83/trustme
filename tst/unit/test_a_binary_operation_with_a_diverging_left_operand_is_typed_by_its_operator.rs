//@ compile-fail: Failed to find an impl
// The left-hand counterpart of `test_a_binary_operation_with_a_diverging_right_operand_has_its_operators_output`:
// upstream coerces a by-value operator's left operand into a fresh variable
// (`check_overloaded_binop`) and types the operation as the operator's
// `Output`. A diverging operand leaves that variable to the fallback, `()`,
// and `(): Add<i32>` does not hold (E0277, "cannot add `i32` to `()`"). We
// typed the whole operation `!` and accepted it.
fn main() {
    let x: u32 = (return) + 1;
    let _ = x;
}
