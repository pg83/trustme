// Extracted from library/core/src/num/uint_macros.rs:2795
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    
    fn quadratic_mul<const N: usize>(a: [u8; N], b: [u8; N]) -> [u8; N] {
        let mut out = [0; N];
        for j in 0..N {
            let mut carry = 0;
            for i in 0..(N - j) {
                (out[j + i], carry) = u8::carrying_mul_add(a[i], b[j], out[j + i], carry);
            }
        }
        out
    }
    
    // -1 * -1 == 1
    assert_eq!(quadratic_mul([0xFF; 3], [0xFF; 3]), [1, 0, 0]);
    
    assert_eq!(u32::wrapping_mul(0x9e3779b9, 0x7f4a7c15), 0xCFFC982D);
    assert_eq!(
        quadratic_mul(u32::to_le_bytes(0x9e3779b9), u32::to_le_bytes(0x7f4a7c15)),
        u32::to_le_bytes(0xCFFC982D)
    );
}
