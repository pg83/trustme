// Extracted from library/alloc/src/borrow.rs:146
#![allow(unused)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::borrow::Cow;
        
        struct Items<'a, X> where [X]: ToOwned<Owned = Vec<X>> {
            values: Cow<'a, [X]>,
        }
        
        impl<'a, X: Clone + 'a> Items<'a, X> where [X]: ToOwned<Owned = Vec<X>> {
            fn new(v: Cow<'a, [X]>) -> Self {
                Items { values: v }
            }
        }
        
        // Creates a container from borrowed values of a slice
        let readonly = [1, 2];
        let borrowed = Items::new((&readonly[..]).into());
        match borrowed {
            Items { values: Cow::Borrowed(b) } => println!("borrowed {b:?}"),
            _ => panic!("expect borrowed value"),
        }
        
        let mut clone_on_write = borrowed;
        // Mutates the data from slice into owned vec and pushes a new value on top
        clone_on_write.values.to_mut().push(3);
        println!("clone_on_write = {:?}", clone_on_write.values);
        
        // The data was mutated. Let's check it out.
        match clone_on_write {
            Items { values: Cow::Owned(_) } => println!("clone_on_write contains owned data"),
            _ => panic!("expect owned data"),
        }
        Ok(())
    }
    doctest().unwrap();
}
