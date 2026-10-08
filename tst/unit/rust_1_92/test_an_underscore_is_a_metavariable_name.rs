// rustc lexes `_` as an identifier (`kw::Underscore`), so `$_` is a
// metavariable named `_`: in a matcher it binds, and in a transcriber it is
// substituted, or written out as `$_` when nothing binds it.
// macro_rules_attribute's `ඞ_with_dollar!` hands a `$` to a nested macro
// definition through `( $_:tt ) => ( .. $_($item:tt)* .. )`.
macro_rules! with_dollar {( $($rules:tt)* ) => (
    macro_rules! __emit__ { $($rules)* }
    __emit__! { $ }
)}

macro_rules! make_sum {( $name:ident ) => (
    with_dollar! {( $_:tt ) => (
        macro_rules! $name {( $_($_ x:expr),* ) => ( 0 $_(+ $_ x)* )}
    )}
)}

make_sum!(sum);

fn main() {
    assert_eq!(sum!(1, 2, 3), 6);
}
