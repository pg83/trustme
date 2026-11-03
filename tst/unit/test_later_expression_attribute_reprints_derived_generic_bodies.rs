//@ proc-macro-aux-build: erase_reprinted_local_functions.rs
//@ compile-variant: -O
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

macro_rules! import_eraser {
    () => { use erase_reprinted_local_functions::erase as pending_erase; };
}

fn main() {
    let erased: u8 = #[pending_erase] {
        #[derive(Clone, Default, PartialEq)]
        struct Container<T> { selected: T }
        fn selected() -> u8 {
            let item = Container { selected: 17u8 };
            item.clone().selected
        }
        selected()
    };
    import_eraser!();
    assert_eq!(erased, 0);
}
