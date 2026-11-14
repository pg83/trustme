//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

macro_rules! make_macro {
    ($v:vis struct $name:ident;) => {
        macro_rules! inner {
            () => { $v struct $name; };
        }
        inner!();
    };
}
make_macro!(struct Marker;);
