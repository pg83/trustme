//@ crate-type: lib
//@ compile-flags: -O -Z dump-mir --emit=metadata

fn a_step(callback: fn(u32) -> u32, value: u32) -> u32 {
    callback(value)
}

fn b_repeat(callback: fn(u32) -> u32, mut value: u32) -> ! {
    value = a_step(callback, value);
    loop {
        value = a_step(callback, value);
    }
}

pub fn run_callback_loop(callback: fn(u32) -> u32, value: u32) -> ! {
    b_repeat(callback, value)
}
