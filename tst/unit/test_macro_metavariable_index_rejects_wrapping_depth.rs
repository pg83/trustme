//@ compile-fail: error:

#![feature(macro_metavar_expr)]

macro_rules! positions {
    ($($value:expr),*) => {
        [$(($value, ${index(4294967041)})),*]
    };
}

fn main() {
    let _ = positions!(701u16, 709u16);
}
