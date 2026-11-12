//@ compile-fail: error:

#![feature(macro_metavar_expr)]

macro_rules! positions {
    ($($value:expr),*) => {
        [$(($value, ${index(512)})),*]
    };
}

fn main() {
    let _ = positions!(719u16, 727u16);
}
