// Extracted from library/core/src/num/uint_macros.rs:2707
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    fn scalar_mul_eq(little_endian_digits: &mut Vec<u16>, multiplicand: u16) {
        let mut carry = 0;
        for d in little_endian_digits.iter_mut() {
            (*d, carry) = d.carrying_mul(multiplicand, carry);
        }
        if carry != 0 {
            little_endian_digits.push(carry);
        }
    }
    
    let mut v = vec![10, 20];
    scalar_mul_eq(&mut v, 3);
    assert_eq!(v, [30, 60]);
    
    assert_eq!(0x87654321_u64 * 0xFEED, 0x86D3D159E38D);
    let mut v = vec![0x4321, 0x8765];
    scalar_mul_eq(&mut v, 0xFEED);
    assert_eq!(v, [0xE38D, 0xD159, 0x86D3]);
}
