//@ crate-type: lib
//@ compile-flags: -O -Z dump-mir --emit=metadata

fn a_step(callback: fn() -> u32) -> u32 {
    callback()
}

fn b_repeat(callback: fn() -> u32, mut _last: u32) -> ! {
    _last = a_step(callback);
    loop {
        _last = a_step(callback);
    }
}

pub fn run_callback_loop(callback: fn() -> u32, last: u32) -> ! {
    b_repeat(callback, last)
}
