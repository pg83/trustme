//@ edition: 2015
//@ proc-macro-aux-build: late_module_attributes.rs
#![allow(bare_trait_objects)]
extern crate late_module_attributes;
#[late_module_attributes::pass]
struct Callback(std::marker::PhantomData<Fn()>);
fn main() {
    let value = Callback(std::marker::PhantomData);
    assert_eq!(std::mem::size_of_val(&value), 0);
}
