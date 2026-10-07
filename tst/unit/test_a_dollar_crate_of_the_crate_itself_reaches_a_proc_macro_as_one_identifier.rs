//@ proc-macro-aux-build: dollar_crate_probe.rs
//@ aux-build: dollar_crate_own_forward.rs
// pwd-grp's `derive_deftly_template_*` macros are its own `macro_rules!` and
// hand `$crate` to derive-deftly's engine, whose syn `Path` parser reads it.
// Upstream passes the one identifier `$crate` whichever crate the macro is
// from; we did so only for another crate's macro and sent our own spelling for
// the library's own - a `::` and a string literal - and syn stopped at the
// literal: "expected identifier".
fn main() {
    assert_eq!(dollar_crate_own_forward::first(), "ident:$crate");
    assert_eq!(dollar_crate_own_forward::value(), 11);
}
