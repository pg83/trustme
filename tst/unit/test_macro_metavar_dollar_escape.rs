// `$$` is an escaped dollar: the expansion emits it literally, which is how a
// macro defines a macro that has metavariables of its own. The transcriber had
// no case for it and rejected the second `$`.
#![feature(macro_metavar_expr)]

macro_rules! define {
    ($name:ident) => {
        macro_rules! $name {
            ( $$( $v:ident ),* ) => {
                $$( fn $v() -> &'static str { stringify!($v) } )*
            };
        }
    };
}

define!(make_fns);
make_fns!(alpha, beta);

fn main() {
    assert_eq!(alpha(), "alpha");
    assert_eq!(beta(), "beta");
}
