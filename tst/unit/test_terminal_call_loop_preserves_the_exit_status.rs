//@ compile-flags: -O -C panic=abort

extern "C" {
    fn tolower(value: i32) -> i32;
    fn _Exit(status: i32);
}

#[inline]
fn finish(letter: i32) -> ! {
    let status = unsafe { tolower(letter) } ^ b'g' as i32;
    loop {
        unsafe { _Exit(status) }
    }
}

fn main() {
    finish(std::hint::black_box(b'G' as i32));
}
