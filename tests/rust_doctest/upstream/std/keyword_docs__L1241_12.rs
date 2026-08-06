// Extracted from library/std/src/keyword_docs.rs:1241
#![allow(unused)]
fn main() {
    fn foo() -> i32 {
        let closure = || {
            return 5;
        };
    
        let future = async {
            return 10;
        };
    
        return 15;
    }
    
    assert_eq!(foo(), 15);
}
