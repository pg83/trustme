// const-evaluated reverse_bits was shifted one bit (miniz_oxide's static table).
static T: [u16; 8] = { let mut t = [0u16; 8]; let mut i = 0; while i < 8 { t[i] = (i as u16).reverse_bits(); i += 1; } t };
fn main() {
    for i in 0..8usize { assert_eq!(T[i], (i as u16).reverse_bits(), "i={}", i); }
    assert_eq!((13u16).reverse_bits(), 0b1011_0000_0000_0000);
}
