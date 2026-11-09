//@ compile-fail: requires a call expression
#![feature(explicit_tail_calls)]

fn main() {
    become;
}
