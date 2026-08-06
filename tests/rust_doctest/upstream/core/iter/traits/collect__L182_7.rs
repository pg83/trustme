// Extracted from library/core/src/iter/traits/collect.rs:182
#![allow(unused)]
fn main() {
    // A sample collection, that's just a wrapper over Vec<T>
    #[derive(Debug)]
    struct MyCollection(Vec<i32>);
    
    // Let's give it some methods so we can create one and add things
    // to it.
    impl MyCollection {
        fn new() -> MyCollection {
            MyCollection(Vec::new())
        }
    
        fn add(&mut self, elem: i32) {
            self.0.push(elem);
        }
    }
    
    // and we'll implement IntoIterator
    impl IntoIterator for MyCollection {
        type Item = i32;
        type IntoIter = std::vec::IntoIter<Self::Item>;
    
        fn into_iter(self) -> Self::IntoIter {
            self.0.into_iter()
        }
    }
    
    // Now we can make a new collection...
    let mut c = MyCollection::new();
    
    // ... add some stuff to it ...
    c.add(0);
    c.add(1);
    c.add(2);
    
    // ... and then turn it into an Iterator:
    for (i, n) in c.into_iter().enumerate() {
        assert_eq!(i as i32, n);
    }
}
