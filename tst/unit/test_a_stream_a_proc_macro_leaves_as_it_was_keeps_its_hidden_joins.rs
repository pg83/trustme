//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// paste's test_path_in_attr: `paste! { m!(#[x = foo::Bar]) }` and `m` does
// `stringify!($x)` on a `$x:ty`. rustc's tokens remember when one follows the
// next with no space (`Spacing::JointHidden`), a proc macro cannot see that,
// and a stream or group it hands back as it got it keeps it: `foo::Bar`.
// Tokens it rebuilds from `TokenTree`s lose it: `foo :: Bar`. Our bridge
// carried no spacing, and everything came back spaced.
use proc_macro_item_passthrough::{echo, paste_idents};
macro_rules! m {
    (#[x = $x:ty]) => {
        stringify!($x)
    };
}
macro_rules! t {
    ($($x:tt)*) => {
        stringify!($($x)*)
    };
}
fn main() {
    assert_eq!(m!(#[x = foo::Bar]), "foo::Bar");
    assert_eq!(echo! { m!(#[x = foo::Bar]) }, "foo::Bar");
    assert_eq!(paste_idents! { m!(#[x = foo::Bar]) }, "foo :: Bar");
    assert_eq!(paste_idents! { m!(#[x = Vec<foo::Bar>]) }, "Vec < foo :: Bar >");
    assert_eq!(paste_idents! { t!(foo::Bar + a.b) }, "foo :: Bar + a.b");
    assert_eq!(paste_idents! { stringify!(foo::Bar + a.b) }, "foo :: Bar + a.b");
    assert_eq!(echo! { stringify!(Vec<foo::Bar>) }, "Vec<foo::Bar>");
    assert_eq!(echo! { t!(a.b::<c>(d)) }, "a.b::<c>(d)");
}
