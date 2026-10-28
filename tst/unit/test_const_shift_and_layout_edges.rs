//@ crate-type: lib
const EMPTY: [i32; 0] = [];
const SHIFT_LEFT: u8 = 1 << 7;
const SHIFT_RIGHT: i8 = -1 >> 7;
const WRAPPING: u8 = 255;
const REFERENCE: &u8 = &SHIFT_LEFT;
const ARRAY: [i32; 2] = [SHIFT_LEFT as i32, SHIFT_RIGHT as i32];

pub fn constants() -> i32 {
    EMPTY.len() as i32 + ARRAY[0] + ARRAY[1] + *REFERENCE as i32 + WRAPPING as i32
}
