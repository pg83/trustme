// Extracted from src/items/type-aliases.md:26
#![allow(unused)]
fn main() {
    struct MyStruct(u32);
    
    use MyStruct as UseAlias;
    type TypeAlias = MyStruct;
    
    let _ = UseAlias(5); // OK
    let _ = TypeAlias(5); // Doesn't work
}
