// Extracted from src/names/scopes.md:71
#![allow(unused)]
fn main() {
    fn shadow_example() {
        // Since there are no local variables in scope yet, this resolves to the function.
        foo(); // prints `function`
        let foo = || println!("closure");
        fn foo() { println!("function"); }
        // This resolves to the local closure since it shadows the item.
        foo(); // prints `closure`
    }
}
