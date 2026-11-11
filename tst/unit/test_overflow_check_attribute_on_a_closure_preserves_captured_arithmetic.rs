#![feature(rustc_attrs, stmt_expr_attributes)]

fn main() {
    let offset = 13u16;
    let add = #[rustc_inherit_overflow_checks] |value: u16| value + offset;
    assert_eq!(add(29), 42);
}
