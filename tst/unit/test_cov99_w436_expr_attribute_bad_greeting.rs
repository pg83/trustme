//@ proc-macro-aux-build: cov99_w436_proc_macro_bad_greeting.rs
//@ compile-fail: proc_macro expansion failed
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

fn main() {
    #[cov99_w436_proc_macro_bad_greeting::reject]
    7;
}
