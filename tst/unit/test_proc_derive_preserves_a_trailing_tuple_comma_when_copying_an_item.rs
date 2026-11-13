//@ compile-fail: error:0: Unexpected token TOK_COMMA
//@ proc-macro-aux-build: pattern_typed_field_derive.rs

macro_rules! copy_item {
    ($item:item) => {
        mod active { $item }
        #[cfg(any())]
        mod inactive { $item }
    };
}

copy_item! {
    #[derive(pattern_typed_field_derive::KeepField)]
    struct Bounded(u8,);
}

fn main() {}
