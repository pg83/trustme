// Extracted from library/std/src/collections/hash/map.rs:180
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::collections::HashMap;
        
        #[derive(Hash, Eq, PartialEq, Debug)]
        struct Viking {
            name: String,
            country: String,
        }
        
        impl Viking {
            /// Creates a new Viking.
            fn new(name: &str, country: &str) -> Viking {
                Viking { name: name.to_string(), country: country.to_string() }
            }
        }
        
        // Use a HashMap to store the vikings' health points.
        let vikings = HashMap::from([
            (Viking::new("Einar", "Norway"), 25),
            (Viking::new("Olaf", "Denmark"), 24),
            (Viking::new("Harald", "Iceland"), 12),
        ]);
        
        // Use derived implementation to print the status of the vikings.
        for (viking, health) in &vikings {
            println!("{viking:?} has {health} hp");
        }
        Ok(())
    }
    doctest().unwrap();
}
