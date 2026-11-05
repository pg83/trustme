//@ aux-build: macro_source.rs
//@ aux-build: private_macro_user.rs
//@ edition: 2015
//@ default-compile-flags: --cap-lints warn
//@ reject-flags: --cfg=reject_private_import --cap-lints deny -Dprivate_macro_use || --cfg=reject_private_import --cap-lints deny -Dprivate_macro_use => is private || is private
//@ compile-variant: --cfg=reject_private_import --cap-lints warn
//@ compile-variant: --cfg=prefer_public_import --cap-lints deny -Dprivate_macro_use
#![cfg_attr(not(any(reject_private_import, prefer_public_import)), warn(private_macro_use))]

#[macro_use]
extern crate private_macro_user;
#[cfg(prefer_public_import)]
#[macro_use]
extern crate macro_source;

fn main() {
    assert_eq!(shout!(), "loud");
    assert_eq!(private_macro_user::uses(), "loud");
}
