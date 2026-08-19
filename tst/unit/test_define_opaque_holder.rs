// `#[define_opaque(X)]` may name something that holds the opaque rather than the
// opaque itself: a struct with a field of that type, or an alias whose bounds
// mention it.
#![feature(type_alias_impl_trait)]

use std::fmt::Debug;

type Foo = impl Debug;

struct Bar {
    foo: Foo,
}

#[define_opaque(Bar)]
fn bar() -> Bar {
    Bar { foo: "foo" }
}

type Inner = impl Send;
type Outer = impl Iterator<Item = Inner>;

#[define_opaque(Outer)]
fn outer() -> Outer {
    vec!["1", "2"].into_iter()
}

fn main() {
    assert_eq!(format!("{:?}", bar().foo), "\"foo\"");
    assert_eq!(outer().count(), 2);
}
