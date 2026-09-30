//@ proc-macro-aux-build: dollar_crate_probe.rs
//@ aux-build: dollar_crate_forward.rs
// rkyv (under generic-array's tests) writes `munge!(let Self { .. } = this)`,
// and munge's `macro_rules! munge { ($($t:tt)*) => {
// $crate::munge_with_path!($crate => $($t)*) } }` hands `$crate` to a proc
// macro whose syn `Path` parser reads it first. Upstream passes it as the one
// identifier `$crate` and resolves it back to the macro's crate when the proc
// macro echoes it. We passed our own spelling of another crate's `$crate` - a
// `::` and a string literal naming the crate - and syn stopped at the literal:
// "expected identifier".
#[macro_use]
extern crate dollar_crate_forward;

use dollar_crate_probe::{echo, first_token};

fn main() {
    assert_eq!(forward!(first_token), "ident:$crate");
    assert_eq!(forward!(echo), 7);
}
