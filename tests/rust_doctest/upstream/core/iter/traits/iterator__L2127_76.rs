// Extracted from library/core/src/iter/traits/iterator.rs:2127
#![allow(unused)]
#![feature(iter_collect_into)]
fn main() {
    
    let a = [1, 2, 3];
    let mut vec: Vec::<i32> = vec![0, 1];
    
    a.iter().map(|x| x * 2).collect_into(&mut vec);
    a.iter().map(|x| x * 10).collect_into(&mut vec);
    
    assert_eq!(vec, vec![0, 1, 2, 4, 6, 10, 20, 30]);
}
