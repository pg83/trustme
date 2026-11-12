//@ compile-fail: invalid value

const BAD: char = unsafe { std::mem::transmute::<u32, char>(0x20_0000) };

fn main() {
    let _ = BAD;
}
