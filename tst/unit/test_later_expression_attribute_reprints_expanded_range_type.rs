//@ proc-macro-aux-build: erase_reprinted_local_functions.rs
//@ compile-variant: -O
#![feature(proc_macro_hygiene, stmt_expr_attributes, pattern_types, pattern_type_macro)]

macro_rules! import_eraser {
    () => { use erase_reprinted_local_functions::erase as pending_erase; };
}

fn main() {
    let erased: u8 = #[pending_erase] {
        fn selected() -> u8 {
            let bounded: std::pat::pattern_type!(u8 is 3..=9) = 7;
            unsafe { std::mem::transmute(bounded) }
        }
        selected()
    };
    import_eraser!();
    assert_eq!(erased, 0);
}
