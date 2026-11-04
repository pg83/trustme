//@ aux-build: delegated_callback_identity.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]

reuse delegated_callback_identity::{any as forwarded_any, fixed as forwarded_fixed};

fn type_id<T: 'static>(_: T) -> std::any::TypeId { std::any::TypeId::of::<T>() }
fn read(value: &u16) -> u16 { *value }
static VALUE: u16 = 317;

fn main() {
    let any = forwarded_any(read);
    let fixed = forwarded_fixed(read);
    assert_eq!(any(&313), 313);
    assert_eq!(fixed(&VALUE), 317);
    assert_ne!(type_id(any), type_id(fixed));
}
