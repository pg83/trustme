//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// time's tests run `#[rstest]` on a function that came out of a proc macro, and
// its `assert_eq!(n as usize, ..)` reached the attribute as `nas usize`. The
// bridge tells a proc macro that `(` is followed by `n` with no space, and the
// macro gave that join to `n`. Upstream keeps it on the delimiter (the group's
// open spacing), where it decides the space after a `{`.
use proc_macro_item_passthrough::{echo, echo_item};

macro_rules! t {
    ($($x:tt)*) => {
        stringify!($($x)*)
    };
}

echo! {
    #[echo_item]
    fn f(n: u8) -> usize {
        assert_eq!(n as usize, 3);
        n as usize
    }
}

fn main() {
    assert_eq!(f(3), 3);
    assert_eq!(echo! { t!(f(n as usize)) }, "f(n as usize)");
    assert_eq!(echo! { t!(x[i as usize]) }, "x[i as usize]");
    assert_eq!(echo! { t!({a} {b } { c}) }, "{a} {b} { c }");
    assert_eq!(t!({a} {b } { c}), "{a} {b} { c }");
}
