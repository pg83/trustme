#![feature(lang_items, no_core, rustc_attrs, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

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

fn main() -> i32 {
    if cfg!(all()) {
        local_scope::value()
    } else {
        1
    }
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
