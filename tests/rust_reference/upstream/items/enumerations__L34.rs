// Extracted from src/items/enumerations.md:34
#![allow(unused)]
fn main() {
    enum Animal {
        Dog,
        Cat,
    }
    
    let mut a: Animal = Animal::Dog;
    a = Animal::Cat;
}
