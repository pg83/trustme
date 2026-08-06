// Extracted from library/std/src/collections/hash/set.rs:72
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::collections::HashSet;
        #[derive(Hash, Eq, PartialEq, Debug)]
        struct Viking {
            name: String,
            power: usize,
        }
        
        let mut vikings = HashSet::new();
        
        vikings.insert(Viking { name: "Einar".to_string(), power: 9 });
        vikings.insert(Viking { name: "Einar".to_string(), power: 9 });
        vikings.insert(Viking { name: "Olaf".to_string(), power: 4 });
        vikings.insert(Viking { name: "Harald".to_string(), power: 8 });
        
        // Use derived implementation to print the vikings.
        for x in &vikings {
            println!("{x:?}");
        }
        Ok(())
    }
    doctest().unwrap();
}
