// Extracted from library/core/src/slice/mod.rs:1921
#![allow(unused)]
fn main() {
    let v = ['a', 'b', 'c'];
    
    {
       let (left, right) = v.split_at(0);
       assert_eq!(left, []);
       assert_eq!(right, ['a', 'b', 'c']);
    }
    
    {
        let (left, right) = v.split_at(2);
        assert_eq!(left, ['a', 'b']);
        assert_eq!(right, ['c']);
    }
    
    {
        let (left, right) = v.split_at(3);
        assert_eq!(left, ['a', 'b', 'c']);
        assert_eq!(right, []);
    }
}
