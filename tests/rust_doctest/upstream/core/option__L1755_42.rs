// Extracted from library/core/src/option.rs:1755
#![allow(unused)]
fn main() {
    let mut x = None;
    
    {
        let y: &mut u32 = x.get_or_insert_default();
        assert_eq!(y, &0);
    
        *y = 7;
    }
    
    assert_eq!(x, Some(7));
}
