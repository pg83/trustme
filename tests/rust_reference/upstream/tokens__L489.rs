// Extracted from src/tokens.md:489
#![allow(unused)]
#![allow(overflowing_literals)]
fn main() {
    123;
    123i32;
    123u32;
    123_u32;
    
    0xff;
    0xff_u8;
    0x01_f32; // integer 7986, not floating-point 1.0
    0x01_e3;  // integer 483, not floating-point 1000.0
    
    0o70;
    0o70_i16;
    
    0b1111_1111_1001_0000;
    0b1111_1111_1001_0000i64;
    0b________1;
    
    0usize;
    
    // These are too big for their type, but are accepted as literal expressions.
    128_i8;
    256_u8;
    
    // This is an integer literal, accepted as a floating-point literal expression.
    5f32;
}
