// Extracted from library/alloc/src/vec/mod.rs:158
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = Vec::new();
    vec.push(1);
    vec.push(2);
    
    assert_eq!(vec.len(), 2);
    assert_eq!(vec[0], 1);
    
    assert_eq!(vec.pop(), Some(2));
    assert_eq!(vec.len(), 1);
    
    vec[0] = 7;
    assert_eq!(vec[0], 7);
    
    vec.extend([1, 2, 3]);
    
    for x in &vec {
        println!("{x}");
    }
    assert_eq!(vec, [7, 1, 2, 3]);
}
