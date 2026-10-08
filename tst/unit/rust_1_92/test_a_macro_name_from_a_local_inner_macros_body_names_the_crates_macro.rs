//@ aux-build: inner_macro_helpers.rs
// approx's `assert_abs_diff_eq!` is `#[macro_export(local_inner_macros)]` and
// expands to `__assert_approx!(abs_diff_eq, ..)`; `__assert_approx!` then
// invokes `$eq!(..)`. Only `assert_abs_diff_eq` is imported. rustc resolves a
// single-segment macro path as `$crate::name` when its identifier comes from
// the expansion of a `local_inner_macros` macro, wherever it is used: here
// `abs_diff_eq` reaches the invocation through a fragment of another macro.
// We rewrote only the names written directly before a `!` in such a macro.
use inner_macro_helpers::assert_close_to;

fn main() {
    assert_close_to!(3, 3);
}
