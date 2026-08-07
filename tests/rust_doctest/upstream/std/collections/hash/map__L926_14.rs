// Extracted from library/std/src/collections/hash/map.rs:926
#![allow(unused)]
fn main() {
    use std::collections::HashMap;
    use std::hash::{Hash, Hasher};

    #[derive(Clone, Copy, Debug)]
    struct S {
        id: u32,
      #[allow(unused)] // prevents a "field `name` is never read" error
        name: &'static str, // ignored by equality and hashing operations
    }

    impl PartialEq for S {
        fn eq(&self, other: &S) -> bool {
            self.id == other.id
        }
    }

    impl Eq for S {}

    impl Hash for S {
        fn hash<H: Hasher>(&self, state: &mut H) {
            self.id.hash(state);
        }
    }

    let j_a = S { id: 1, name: "Jessica" };
    let j_b = S { id: 1, name: "Jess" };
    let p = S { id: 2, name: "Paul" };
    assert_eq!(j_a, j_b);

    let mut map = HashMap::new();
    map.insert(j_a, "Paris");
    assert_eq!(map.get_key_value(&j_a), Some((&j_a, &"Paris")));
    assert_eq!(map.get_key_value(&j_b), Some((&j_a, &"Paris"))); // the notable case
    assert_eq!(map.get_key_value(&p), None);
}
