//@ proc-macro-aux-build: own_root_use_passthrough.rs
//@ compile-fail: proc macro panicked
macro_rules! import_through {
    ($item:item) => {
        own_root_use_passthrough::preserve!($item);
    };
}

import_through! {
    use {std::mem::size_of, std::mem::align_of};
}

fn main() {
    assert_eq!(size_of::<[u16; 3]>(), 6);
    assert_eq!(align_of::<[u16; 3]>(), 2);
}
