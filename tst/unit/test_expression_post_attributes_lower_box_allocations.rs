//@ compile-variant: -O
#![feature(rustc_attrs, stmt_expr_attributes)]

fn main() {
    let boxed = #[rustc_box] Box::new([73u32, 79]);
    assert_eq!(boxed[0] + boxed[1], 152);
}
