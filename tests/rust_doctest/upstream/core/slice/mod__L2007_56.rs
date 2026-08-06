// Extracted from library/core/src/slice/mod.rs:2007
#![allow(unused)]
fn main() {
    let v = ['a', 'b', 'c'];
    
    unsafe {
       let (left, right) = v.split_at_unchecked(0);
       assert_eq!(left, []);
       assert_eq!(right, ['a', 'b', 'c']);
    }
    
    unsafe {
        let (left, right) = v.split_at_unchecked(2);
        assert_eq!(left, ['a', 'b']);
        assert_eq!(right, ['c']);
    }
    
    unsafe {
        let (left, right) = v.split_at_unchecked(3);
        assert_eq!(left, ['a', 'b', 'c']);
        assert_eq!(right, []);
    }
}
