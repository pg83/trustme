#![feature(lang_items, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

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

macro_rules! classify_fragment {
    ($item:item) => {
        1
    };
    ($statement:stmt) => {
        0
    };
}

macro_rules! classify_item_statement {
    ($statement:stmt) => {
        classify_fragment!($statement)
    };
}

macro_rules! duplicate_item_statement {
    ($statement:stmt) => {{
        { $statement; }
        { $statement; }
    }};
}

macro_rules! require_item_fragment {
    ($statement:stmt) => {
        return 1;
    };
    ($item:item) => {};
}

macro_rules! forward_item_fragment {
    ($item:item) => {
        require_item_fragment!($item);
    };
}

fn main() -> i32 {
    forward_item_statements!(
        struct First;,
        struct Second;,
        let _first = First,
        let _second = Second,
        declare_local! { _value },
        0
    );
    duplicate_item_statement!(struct Repeated;);
    forward_item_fragment!(struct Fourth;);
    classify_item_statement!(struct Third;)
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
