// Extracted from src/types/closure.md:562
#![allow(unused)]
fn main() {
    struct S(String);
    
    let b = Box::new(S(String::new()));
    let c_box = || {
        let x = &(*b).0; // captures `(*b).0` by ImmBorrow
    };
    c_box();
    
    // Contrast `Box` with another type that implements Deref:
    let r = std::rc::Rc::new(S(String::new()));
    let c_rc = || {
        let x = &(*r).0; // captures `r` by ImmBorrow
    };
    c_rc();
}
