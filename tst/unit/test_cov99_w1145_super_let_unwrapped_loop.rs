//@ compile-only
//@ proc-macro-aux-build: cov99_w1145_loop_body.rs
#![feature(custom_inner_attributes, proc_macro_hygiene, stmt_expr_attributes, super_let)]

struct Slot(());

fn main() {
    let _never: () = loop {
        #![cov99_w1145_loop_body::without_block]
    };
}
