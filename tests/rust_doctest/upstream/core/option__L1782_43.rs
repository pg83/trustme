// Extracted from library/core/src/option.rs:1782
#![allow(unused)]
fn main() {
    let mut x = None;
    
    {
        let y: &mut u32 = x.get_or_insert_with(|| 5);
        assert_eq!(y, &5);
    
        *y = 7;
    }
    
    assert_eq!(x, Some(7));
}
