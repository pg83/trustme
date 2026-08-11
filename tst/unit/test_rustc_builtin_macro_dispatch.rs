#![feature(lang_items, no_core, rustc_attrs)]
#![no_core]
#![no_main]
//@ compile-flags: -Clink-arg=-lc

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[rustc_builtin_macro]
macro_rules! cfg {
    () => {
        false
    };
}

mod local_scope {
    macro_rules! cfg {
        () => {
            false
        };
    }

    pub fn value() -> i32 {
        if cfg!() { 1 } else { 0 }
    }
}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    if cfg!(all()) {
        local_scope::value()
    } else {
        1
    }
}
