// Extracted from src/types/closure.md:460
#![allow(unused)]
fn main() {
    struct T(String, String);
    
    let t = T(String::from("foo"), String::from("bar"));
    let t_ptr = &t as *const T;
    
    let c = || unsafe {
        println!("{}", (*t_ptr).0); // captures `t_ptr` by ImmBorrow
    };
    c();
}
