//@ compile-fail: depth parameter of meta-variable expression `count`
#![feature(macro_metavar_expr)]

macro_rules! count_values {
    ($($value:ident),*) => {
        ${count($value, 18446744073709551616)}
    };
}

fn main() {
    let _ = count_values!(left, middle, right);
}
