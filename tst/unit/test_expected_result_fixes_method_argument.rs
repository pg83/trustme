// The expected result of a call fixes the method's type parameter, and the
// argument then only has to reach it. Relating the argument first instead reads
// its own type into that parameter, and a function item names itself rather
// than the pointer it would become: `T` came out as the item's own type and the
// call no longer produced what the binding asked for.

struct Foo;

impl Foo {
    fn foo<T>(self, x: T) -> Option<T> {
        Some(x)
    }
}

fn target() {}

fn main() {
    let inherent: Option<fn()> = Foo.foo(target);
    let turbofished: Option<fn()> = Foo.foo::<_>(target);
    assert!(inherent.is_some());
    assert!(turbofished.is_some());
}
