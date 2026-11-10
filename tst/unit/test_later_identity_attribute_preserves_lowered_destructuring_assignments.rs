//@ proc-macro-aux-build: own_lowered_assignment_identity.rs
#![feature(proc_macro_hygiene, stmt_expr_attributes)]

macro_rules! import_identity {
    () => {
        use own_lowered_assignment_identity::identity as assignment_identity;
    };
}

fn main() {
    let mut first = 3u16;
    let mut second = 5u16;
    #[assignment_identity]
    {
        (first, second) = (second + 36, first + 40);
    }
    import_identity!();
    assert_eq!((first, second), (41, 43));
}
