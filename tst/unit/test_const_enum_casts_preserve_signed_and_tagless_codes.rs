#[repr(i8)]
enum Code { Before = -37, After = 41 }
enum Single { Only = 71 }
const fn signed(value: Code) -> i16 { value as i16 }
const fn unsigned(value: Code) -> u8 { value as u8 }
const fn single(value: Single) -> u64 { value as u64 }
const CODES: (i16, u8, i16, u64) = (
    signed(Code::Before), unsigned(Code::Before), signed(Code::After), single(Single::Only),
);
fn main() { assert_eq!(CODES, (-37, 219, 41, 71)); }
