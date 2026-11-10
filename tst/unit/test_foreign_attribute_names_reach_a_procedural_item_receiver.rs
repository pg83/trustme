//@ aux-build: own_foreign_attribute_source.rs
//@ proc-macro-aux-build: own_attribute_token_observer.rs

macro_rules! receive_item {
    ($item:item) => {
        own_attribute_token_observer::observe!($item);
    };
}

own_foreign_attribute_source::emit!(receive_item);

fn main() {
    assert_eq!(observed(), 59);
}
