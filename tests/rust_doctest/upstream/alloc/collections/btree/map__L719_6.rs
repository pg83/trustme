// Extracted from library/alloc/src/collections/btree/map.rs:719
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::cmp::Ordering;
    use std::collections::BTreeMap;
    
    #[derive(Clone, Copy, Debug)]
    struct S {
        id: u32,
      #[allow(unused)] // prevents a "field `name` is never read" error
        name: &'static str, // ignored by equality and ordering operations
    }
    
    impl PartialEq for S {
        fn eq(&self, other: &S) -> bool {
            self.id == other.id
        }
    }
    
    impl Eq for S {}
    
    impl PartialOrd for S {
        fn partial_cmp(&self, other: &S) -> Option<Ordering> {
            self.id.partial_cmp(&other.id)
        }
    }
    
    impl Ord for S {
        fn cmp(&self, other: &S) -> Ordering {
            self.id.cmp(&other.id)
        }
    }
    
    let j_a = S { id: 1, name: "Jessica" };
    let j_b = S { id: 1, name: "Jess" };
    let p = S { id: 2, name: "Paul" };
    assert_eq!(j_a, j_b);
    
    let mut map = BTreeMap::new();
    map.insert(j_a, "Paris");
    assert_eq!(map.get_key_value(&j_a), Some((&j_a, &"Paris")));
    assert_eq!(map.get_key_value(&j_b), Some((&j_a, &"Paris"))); // the notable case
    assert_eq!(map.get_key_value(&p), None);
}
