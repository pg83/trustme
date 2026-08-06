// Extracted from src/items/enumerations.md:47
#![allow(unused)]
fn main() {
    enum Animal {
        Dog(String, f64),
        Cat { name: String, weight: f64 },
    }
    
    let mut a: Animal = Animal::Dog("Cocoa".to_string(), 37.2);
    a = Animal::Cat { name: "Spotty".to_string(), weight: 2.7 };
}
