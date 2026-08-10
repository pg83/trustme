// Extracted from src/items/constant-items.md:76
#![allow(unused)]
fn main() {
    const _: () =  { struct _SameNameTwice; };
    
    // OK although it is the same name as above:
    const _: () =  { struct _SameNameTwice; };
}
