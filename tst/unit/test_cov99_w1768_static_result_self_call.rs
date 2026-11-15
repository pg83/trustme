//@ crate-type: lib
//@ compile-flags: --emit=metadata -O -Z dump-mir

mod cycle {
    static mut LAST: u8 = 0;

    fn step() {
        unsafe { LAST = repeat(); }
    }

    pub fn repeat() -> u8 {
        step();
        loop {
            step();
        }
    }
}

pub fn entry() -> u8 {
    cycle::repeat()
}
