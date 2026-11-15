//@ edition: 2018
#![feature(decl_macro)]

pub macro panic_from_definition() {
    panic!("panic from an edition 2018 definition")
}

pub macro unreachable_from_definition() {
    unreachable!("unreachable from an edition 2018 definition")
}
