// Extracted from library/std/src/collections/mod.rs:367
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    use std::collections::BTreeMap;
    use std::hash::{Hash, Hasher};

    #[derive(Debug)]
    struct Foo {
        a: u32,
        b: &'static str,
    }

    // we will compare `Foo`s by their `a` value only.
    impl PartialEq for Foo {
        fn eq(&self, other: &Self) -> bool { self.a == other.a }
    }

    impl Eq for Foo {}

    // we will hash `Foo`s by their `a` value only.
    impl Hash for Foo {
        fn hash<H: Hasher>(&self, h: &mut H) { self.a.hash(h); }
    }

    impl PartialOrd for Foo {
        fn partial_cmp(&self, other: &Self) -> Option<Ordering> { self.a.partial_cmp(&other.a) }
    }

    impl Ord for Foo {
        fn cmp(&self, other: &Self) -> Ordering { self.a.cmp(&other.a) }
    }

    let mut map = BTreeMap::new();
    map.insert(Foo { a: 1, b: "baz" }, 99);

    // We already have a Foo with an a of 1, so this will be updating the value.
    map.insert(Foo { a: 1, b: "xyz" }, 100);

    // The value has been updated...
    assert_eq!(map.values().next().unwrap(), &100);

    // ...but the key hasn't changed. b is still "baz", not "xyz".
    assert_eq!(map.keys().next().unwrap().b, "baz");
}
