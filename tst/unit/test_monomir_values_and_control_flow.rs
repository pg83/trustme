//@ crate-type: lib
//@ compile-flags: -Ccodegen-type=monomir -Cemit-cpp-only --env-set TRUSTME_DUMP=ast:hir:mir -Zmir-opt-level=0

pub static TEXT: &str = "zero\0 slash\\ quote\" tab\t line\n\u{7} é";
pub static BYTES: &[u8] = b"\0\x01\x0f\xff\\\"";
pub static FLOATS: [f64; 3] = [0.0, -1.5, 2.25];

pub struct Tuple(pub i32, pub u8);
pub struct Unit;
pub enum Choice { Empty, Pair(i32, u8), Named { value: u64 } }

pub fn constructors() -> (fn(i32, u8) -> Tuple, fn(i32, u8) -> Choice) {
    (Tuple, Choice::Pair)
}

pub fn arithmetic(mut a: i32, b: i32) -> (i32, bool) {
    a *= b;
    a /= b;
    a %= b;
    a &= b;
    a |= b;
    a ^= b;
    a <<= 1;
    a >>= 1;
    let mask = !a;
    let signed = -b;
    let value = ((a + b) * (a - b) / b % b) ^ ((a & b) | (a << 2) | (b >> 2));
    (value + mask + signed, (a <= b && a >= b) || (a != b && a > b))
}

pub fn casts(value: f64, ptr: *mut u8) -> (i16, f32, usize, *const u32) {
    (value as i16, value as f32, ptr as usize, ptr as *const u32)
}

pub fn literals() -> (char, char, char, char, f32, f64, u128) {
    ('\'', '\\', '\n', 'λ', 1.25f32, -2.5f64, 18446744073709551617u128)
}

pub fn match_values(value: i64, unsigned: u64, text: &str) -> u32 {
    let a = match value { -7 => 1, 0 => 2, 9 => 3, _ => 4 };
    let b = match unsigned { 0 => 1, 3 => 2, 11 => 3, _ => 4 };
    let c = match text { "" => 1, "hello" => 2, "world" => 3, _ => 4 };
    a + b + c
}

pub fn patterns(choice: Choice, values: &[i32], pair: (i32, i32, i32)) -> i32 {
    let (first, .., last) = pair;
    let selected = match choice {
        Choice::Empty => 0,
        Choice::Pair(ref value, 0..=5) => *value,
        Choice::Pair(value @ 1..=10, _) => value,
        Choice::Pair(_, _) | Choice::Named { value: 0 } => 11,
        Choice::Named { value } => value as i32,
    };
    let ends = match values {
        [] => 0,
        [only] => *only,
        [head, middle @ .., tail] => *head + *tail + middle.len() as i32,
    };
    selected + ends + first + last
}

pub fn calls(function: fn(i32) -> i32, values: &mut [i32; 4]) -> i32 {
    let last = values[3];
    let [first, ref mut second, ..] = *values;
    *second += function(first);
    let closure = |x: i32| function(x) + *second;
    closure(last)
}

pub fn labelled(mut count: i32) -> i32 {
    'outer: loop {
        while count > 0 {
            count -= 1;
            if count == 4 { continue 'outer; }
            if count == 2 { break 'outer count; }
        }
        break 0;
    }
}

pub fn borrow_raw(value: &mut i32) -> (*const i32, *mut i32) {
    (&raw const *value, &raw mut *value)
}

pub fn arrays(value: u8) -> ([u8; 4], [u8; 3]) {
    ([value; 4], [1, 2, value])
}

pub fn fallible(value: Result<i32, u8>) -> Result<i32, u8> {
    Ok(value? + 1)
}

pub unsafe fn assembly(mut value: usize) -> usize {
    core::arch::asm!("/* {0} {1} */", inout(reg) value, const 7, options(nomem, nostack));
    value
}
