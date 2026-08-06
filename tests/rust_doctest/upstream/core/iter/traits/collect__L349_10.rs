// Extracted from library/core/src/iter/traits/collect.rs:349
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
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
        
        // since MyCollection has a list of i32s, we implement Extend for i32
        impl Extend<i32> for MyCollection {
        
            // This is a bit simpler with the concrete type signature: we can call
            // extend on anything which can be turned into an Iterator which gives
            // us i32s. Because we need i32s to put into MyCollection.
            fn extend<T: IntoIterator<Item=i32>>(&mut self, iter: T) {
        
                // The implementation is very straightforward: loop through the
                // iterator, and add() each element to ourselves.
                for elem in iter {
                    self.add(elem);
                }
            }
        }
        
        let mut c = MyCollection::new();
        
        c.add(5);
        c.add(6);
        c.add(7);
        
        // let's extend our collection with three more numbers
        c.extend(vec![1, 2, 3]);
        
        // we've added these elements onto the end
        assert_eq!("MyCollection([5, 6, 7, 1, 2, 3])", format!("{c:?}"));
        Ok(())
    }
    doctest().unwrap();
}
