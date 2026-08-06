// Extracted from library/core/src/iter/traits/collect.rs:53
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
    
    // and we'll implement FromIterator
    impl FromIterator<i32> for MyCollection {
        fn from_iter<I: IntoIterator<Item=i32>>(iter: I) -> Self {
            let mut c = MyCollection::new();
    
            for i in iter {
                c.add(i);
            }
    
            c
        }
    }
    
    // Now we can make a new iterator...
    let iter = (0..5).into_iter();
    
    // ... and make a MyCollection out of it
    let c = MyCollection::from_iter(iter);
    
    assert_eq!(c.0, vec![0, 1, 2, 3, 4]);
    
    // collect works too!
    
    let iter = (0..5).into_iter();
    let c: MyCollection = iter.collect();
    
    assert_eq!(c.0, vec![0, 1, 2, 3, 4]);
}
