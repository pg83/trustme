//@ proc-macro-aux-build: use_item_identity.rs
#![feature(associated_type_defaults, inherent_associated_types, extern_types)]

macro_rules! forward {
    ($item:item) => { use_item_identity::echo!($item); };
}

forward! {
    type Callback = for<'a> fn(&'a u16) -> &'a u16;
}

forward! {
    trait Family {
        type Item;
        type Pair = (Self::Item, Self::Item);
    }
}

struct Source;

forward! {
    impl Family for Source {
        type Item = u16;
    }
}

forward! {
    impl Source {
        pub type Block = [u16; 3];
    }
}

forward! {
    extern "C" {
        type Foreign;
    }
}

fn borrow(value: &u16) -> &u16 {
    value
}

fn main() {
    let callback: Callback = borrow;
    assert_eq!(*callback(&37), 37);
    let pair: <Source as Family>::Pair = (41, 43);
    assert_eq!(pair, (41, 43));
    let block: Source::Block = [3, 5, 7];
    assert_eq!(block, [3, 5, 7]);
    let opaque: *const Foreign = std::ptr::null();
    assert!(opaque.is_null());
}
