//@ proc-macro-aux-build: erase_reprinted_local_functions.rs
//@ compile-variant: -O
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

macro_rules! import_eraser {
    () => { use erase_reprinted_local_functions::erase as pending_erase; };
}

fn main() {
    let erased: u8 = #[pending_erase] {
        fn selected<T>() {
            let _: impl 'static = ();
            let _: impl ?Sized + 'static = ();
            let _: impl Clone + ?Sized + 'static + use<T> = ();
        }
        selected::<u8>();
        23u8
    };
    import_eraser!();
    assert_eq!(erased, 0);
}
