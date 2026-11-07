//@ compile-fail: count
#![feature(macro_metavar_expr)]

macro_rules! count_each_element {
    ($( $item:ident ),+) => {
        [$( (${count($item)}, stringify!($item)) ),+]
    };
}

fn main() {
    let _ = count_each_element!(alpha, beta);
}
