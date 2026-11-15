//@ edition: 2021
//@ aux-build: cov99_w1185_plain_trait.rs
#![feature(fn_delegation, decl_macro)]

macro delegate($alias:ident) {
    reuse <u32 as cov99_w1185_plain_trait::Value>::value as $alias;
}

delegate!(delegated_value);

fn main() {
    assert_eq!(delegated_value(37), 42);
}
