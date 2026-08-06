// Extracted from library/core/src/iter/traits/iterator.rs:1978
#![allow(unused)]
fn main() {
    let chars = ['g', 'd', 'k', 'k', 'n'];
    
    let hello: String = chars.into_iter()
        .map(|x| x as u8)
        .map(|x| (x + 1) as char)
        .collect();
    
    assert_eq!("hello", hello);
}
