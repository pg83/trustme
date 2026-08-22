//@ crate-type: lib

#![feature(decl_macro)]

pub macro make_struct() {
    struct S;
}

make_struct!();
