//@ proc-macro-aux-build: keep_generated_literals.rs
//@ input: aux/generated_literal_bytes.txt
//@ compile-variant: -O
#![feature(proc_macro_hygiene, stmt_expr_attributes, concat_bytes)]

macro_rules! import_keep {
    () => { use keep_generated_literals::keep as pending_keep; };
}

fn main() {
    let values = #[pending_keep] {
        (concat_bytes!(b"A", b'\n', [0u8, 255u8]), include_bytes!("aux/generated_literal_bytes.txt"), line!(), column!())
    };
    import_keep!();
    assert_eq!(values.0, b"A\n\0\xff");
    assert_eq!(values.1, b"own bytes\n");
    assert!(values.2 > 0);
    assert!(values.3 > 0);
}
