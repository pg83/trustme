//@ crate-type: lib
//@ compile-flags: --emit=metadata -O -Z dump-mir

mod cycle {
    fn step(out: &mut u8) {
        *out = repeat(out);
    }

    pub fn repeat(out: &mut u8) -> u8 {
        step(out);
        loop {
            step(out);
        }
    }
}

pub fn entry(out: &mut u8) -> u8 {
    cycle::repeat(out)
}
