//@ crate-type: lib
//@ edition: 2024
//@ input: support/prelude_sized/sync/mod.rs
//@ input: support/prelude_sized/sync/atomic.rs
#![feature(no_core, lang_items, prelude_import, rustc_attrs)]
#![no_core]

#[prelude_import]
use marker::*;

mod marker {
    #[lang = "pointee_sized"]
    pub trait PointeeSized {}
    #[lang = "meta_sized"]
    pub trait MetaSized: PointeeSized {}
    #[lang = "sized"]
    pub trait Sized: MetaSized {}
}

#[path = "support/prelude_sized/sync/mod.rs"]
mod sync;
