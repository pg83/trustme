//@ compile-fail: concat with non-ident
#![feature(macro_metavar_expr_concat)]

macro_rules! invalid {
    ($value:expr) => {
        const ${concat(value_, $value)}: u8 = 0;
    };
}

invalid!(3u16 + 5u16);

fn main() {}
