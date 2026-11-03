//@ compile-variant: -O
extern "C" { fn getpid() -> i32; fn getpgid(process: i32) -> i32; }
#[inline]
fn small() -> ! { loop { unsafe { getpid(); } } }
#[inline]
fn large(mut value: u32) -> ! {
    loop {
        value = ((value << 3) | (value >> 29)) ^ 17;
        value = ((value << 5) | (value >> 27)) ^ 19;
        value = ((value << 7) | (value >> 25)) ^ 23;
        value = ((value << 9) | (value >> 23)) ^ 29;
        value = ((value << 11) | (value >> 21)) ^ 31;
        value = ((value << 13) | (value >> 19)) ^ 37;
        unsafe { getpgid(value as i32); }
    }
}
fn select(mode: u8) -> u32 {
    match mode { 1 => small(), 2 => large(167), _ => 173 }
}
fn main() { assert_eq!(select(std::hint::black_box(0)), 173); }
