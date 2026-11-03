//@ compile-variant: -O
#![allow(macro_expanded_macro_exports_accessed_by_absolute_paths)]

macro_rules! install {
    () => {
        #[macro_export]
        macro_rules! block_exported_value { () => { 71u32 }; }
    };
}

fn main() {
    crate::block_exported_value!();
    install!();
    assert_eq!(crate::block_exported_value!(), 71);
}
