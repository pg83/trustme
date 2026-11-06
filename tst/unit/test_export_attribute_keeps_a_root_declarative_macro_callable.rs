#![feature(decl_macro)]

#[macro_export]
pub macro sum($left:expr, $right:expr) {
    $left + $right
}

fn main() {
    assert_eq!(crate::sum!(31u16, 12u16), 43);
}
