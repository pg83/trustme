//@ proc-macro-aux-build: cov99_w263_reprint.rs
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-w263
#![allow(unused_parens)]
#![allow(macro_expanded_macro_exports_accessed_by_absolute_paths)]

macro_rules! record_expr {
    ($value:expr) => { split_expr!(($value)) };
}

macro_rules! split_expr {
    ($value:expr) => {{
        crate::w263_late!($value);
        let _ = $value;
    }};
}

macro_rules! install_late {
    () => {
        #[macro_export]
        macro_rules! w263_late {
            ($value:expr) => { cov99_w263_reprint::assert_fragment!($value) };
        }
    };
}

fn main() {
    record_expr!({
        unsafe { core::arch::asm!("", options(nomem, nostack, preserves_flags)); }
        true
    });
}

install_late!();
