// Extracted from src/items/constant-items.md:86
#![allow(unused)]
fn main() {
    macro_rules! m {
        ($item: item) => { $item $item }
    }
    
    m!(const _: () = (););
    // This expands to:
    // const _: () = ();
    // const _: () = ();
}
