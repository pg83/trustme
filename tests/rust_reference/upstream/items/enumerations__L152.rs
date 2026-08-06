// Extracted from src/items/enumerations.md:152
#![allow(unused)]
fn main() {
    enum Foo {
        Bar,            // 0
        Baz = 123,      // 123
        Quux,           // 124
    }
    
    let baz_discriminant = Foo::Baz as u32;
    assert_eq!(baz_discriminant, 123);
}
