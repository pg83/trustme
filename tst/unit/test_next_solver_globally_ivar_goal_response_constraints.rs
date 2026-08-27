//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// A goal still holding inference variables (`&[?e; 4]: IntoIterator`) is
// evaluated in the solver's canonical inference space. Its selected response
// binds T := ?e, so `10_usize.pow(pow)` can constrain the element to u32 before
// the literal fallback defaults it to i32. No legacy possibilities walk is
// allowed after the solver response. Mirrors libtest bench.rs
// fmt_thousands_sep.

pub fn f() -> usize {
    let mut total = 0usize;
    for &pow in &[9, 6, 3, 0] {
        total += 10_usize.pow(pow);
    }
    total
}
