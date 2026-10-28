//@ aux-build: resolution.rs
//@ crate-type: lib
extern crate resolution;

use resolution::exported::{Alias, CONSTANT};
use resolution::outer::{Item, Trait};

mod nested {
    pub use super::resolution::outer as api;
    pub type Local = super::Alias;
}

pub fn resolve(item: Item) -> i32 {
    let alias: Alias = item;
    let local: nested::Local = alias;
    let _ = nested::api::inner::CONSTANT;
    local.value() + CONSTANT
}
