//@ aux-build: macro_source.rs
//@ aux-build: private_macro_user.rs
//@ edition: 2015
//@ compile-fail: must be at the crate root
#![cfg_attr(not(reject_private_import), warn(private_macro_use))]

mod compatibility_import {
    #[macro_use]
    extern crate private_macro_user;

    pub fn value() -> &'static str {
        shout!()
    }
}

mod public_import_wins {
    #[macro_use]
    extern crate macro_source;
    #[macro_use]
    extern crate private_macro_user;

    pub fn value() -> &'static str {
        shout!()
    }
}

fn main() {
    assert_eq!(compatibility_import::value(), "loud");
    assert_eq!(public_import_wins::value(), "loud");
}
