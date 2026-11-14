//@ compile-only
//@ compile-flags: --emit=metadata
//@ proc-macro-aux-build: cov99_w367_uniterated_fragment.rs
//@ env: TRUSTME_DUMP_PROCMACRO=$OUT-transport

macro_rules! unit_value {
    () => { () };
}

macro_rules! capture_expr {
    ($e:expr) => { keep_block!({ $e; }); };
}

macro_rules! keep_block {
    ($b:block) => {
        late_fragment!($b);
        { late_fragment!($b) };
        $b;
    };
}

macro_rules! install_fragment {
    () => {
        use cov99_w367_uniterated_fragment::recover as late_fragment;
    };
}

fn main() {
    capture_expr!(unit_value!());
    install_fragment!();
}
