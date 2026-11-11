//@ compile-fail: expected non-macro inner attribute
//@ proc-macro-aux-build: unwrap_else_tail.rs
//@ compile-flags: -Zdump-ast
#![feature(custom_inner_attributes, proc_macro_hygiene, stmt_expr_attributes, ergonomic_clones)]
#![allow(incomplete_features)]

use std::rc::Rc;
use unwrap_else_tail::tail;

fn choose(first: bool) -> u32 {
    if first {
        13
    } else {
        {
            #![tail]
            17
        }
    }
}

fn main() {
    assert_eq!(choose(true), 13);
    let value = Rc::new(choose(false));
    let take = use || value;
    let selected = take();
    assert_eq!(*selected, 17);
    assert!(Rc::ptr_eq(&value, &selected));
    drop(selected);
    assert_eq!(Rc::strong_count(&value), 1);
}
