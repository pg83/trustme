//@ run-pass
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// proc-macro-crate's `tests/workspace_deps` ends `fn use_it()` with
// `my_cool_dep::do_something!()`, a procedural macro that hands back its
// empty input. rustc expands a macro call closing a block as an optional
// expression; a procedural macro's output is parsed as that fragment
// (`AstFragmentKind::OptExpr`), and an empty one drops the statement, so the
// block has no value of its own. (A `macro_rules!` expansion there is made
// into an expression and may not be empty.)
extern crate proc_macro_item_passthrough;

use proc_macro_item_passthrough::echo;

fn closes_with_nothing() {
    echo!()
}

fn closes_after_a_statement() {
    let _ = 1;
    echo!()
}

fn main() {
    let () = closes_with_nothing();
    let () = closes_after_a_statement();
}
