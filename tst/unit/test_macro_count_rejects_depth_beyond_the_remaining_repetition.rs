//@ compile-fail: depth parameter of meta-variable expression `count`
#![feature(macro_metavar_expr)]

macro_rules! count_each {
    ($( [$($value:ident),*] );*) => {
        [$( ${count($value, 1)}, )*]
    };
}

fn main() {
    let _ = count_each!([left, right]; [tail]);
}
