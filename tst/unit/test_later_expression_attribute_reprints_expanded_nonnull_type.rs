//@ proc-macro-aux-build: erase_reprinted_local_functions.rs
//@ compile-variant: -O
#![feature(proc_macro_hygiene, stmt_expr_attributes, pattern_types, pattern_type_macro)]

macro_rules! import_eraser {
    () => { use erase_reprinted_local_functions::erase as pending_erase; };
}

fn main() {
    let erased: u8 = #[pending_erase] {
        fn selected() {
            let _: std::pat::pattern_type!(*const u8 is !null) = std::ptr::dangling::<u8>();
        }
        selected();
        11u8
    };
    import_eraser!();
    assert_eq!(erased, 0);
}
