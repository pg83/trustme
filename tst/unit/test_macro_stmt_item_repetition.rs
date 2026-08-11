#![feature(lang_items, no_core)]
#![no_core]
#![no_main]
//@ compile-flags: -Clink-arg=-lc

#[lang = "pointee_sized"]
pub trait PointeeSized {}

#[lang = "meta_sized"]
pub trait MetaSized: PointeeSized {}

#[lang = "sized"]
pub trait Sized: MetaSized {}

#[lang = "copy"]
pub trait Copy {}

impl Copy for i32 {}

macro_rules! item_statements {
    ($($statement:stmt),+) => {
        $($statement;)+
    };
}

macro_rules! forward_item_statements {
    ($($statement:stmt),+) => {
        item_statements!($($statement),+);
    };
}

macro_rules! declare_local {
    ($name:ident) => {
        let $name = 0;
    };
}

macro_rules! duplicate_item_statement {
    ($statement:stmt) => {{
        { $statement; }
        { $statement; }
    }};
}

#[no_mangle]
extern "C-unwind" fn main() -> i32 {
    forward_item_statements!(
        struct First;,
        struct Second;,
        let _first = First,
        let _second = Second,
        declare_local! { _value }
    );
    duplicate_item_statement!(struct Repeated;);
    0
}
