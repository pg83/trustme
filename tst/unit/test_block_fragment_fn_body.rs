// A function body may come from a `block` fragment. Free functions and impl
// methods took one; a trait's default body did not, and the error even named
// the function rather than the token it stopped on.
//
// Same shape as the upstream test macros/macro-as-fn-body.rs.
macro_rules! defFn {
    ($body:block) => {
        fn bar() -> u32 $body
    };
}

trait Foo {
    defFn!({ 1 });
}

struct Baz {}

impl Foo for Baz {}

struct Qux {}

impl Qux {
    defFn!({ 2 });
}

defFn!({ 3 });

fn main() {
    assert_eq!(Baz::bar(), 1);
    assert_eq!(Qux::bar(), 2);
    assert_eq!(bar(), 3);
}
