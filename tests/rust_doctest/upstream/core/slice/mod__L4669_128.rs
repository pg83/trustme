// Extracted from library/core/src/slice/mod.rs:4669
#![allow(unused)]
fn main() {
    let x = &mut [1, 2, 4];
    
    unsafe {
        let [a, b] = x.get_disjoint_unchecked_mut([0, 2]);
        *a *= 10;
        *b *= 100;
    }
    assert_eq!(x, &[10, 2, 400]);
    
    unsafe {
        let [a, b] = x.get_disjoint_unchecked_mut([0..1, 1..3]);
        a[0] = 8;
        b[0] = 88;
        b[1] = 888;
    }
    assert_eq!(x, &[8, 88, 888]);
    
    unsafe {
        let [a, b] = x.get_disjoint_unchecked_mut([1..=2, 0..=0]);
        a[0] = 11;
        a[1] = 111;
        b[0] = 1;
    }
    assert_eq!(x, &[1, 11, 111]);
}
