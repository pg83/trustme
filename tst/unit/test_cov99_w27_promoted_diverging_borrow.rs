//@ edition: 2021
#![allow(dead_code, unreachable_code)]

const fn stop() -> ! {
    loop {}
}

struct Probe<T>(T);

impl<T> Probe<T> {
    const VALUE: () = {
        let _: &&mut T = &{ stop(); };
    };
}

fn main() {}
