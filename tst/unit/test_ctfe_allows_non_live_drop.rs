#![feature(const_precise_live_drops)]

static mut DROPS: usize = 0;

struct CountDrop;

impl Drop for CountDrop {
    fn drop(&mut self) {
        unsafe {
            DROPS += 1;
        }
    }
}

const RETURNED: CountDrop = CountDrop;
const RETURNED_REFERENCE: &'static CountDrop = &CountDrop;

const INACTIVE_VARIANT: () = {
    let _value: Option<CountDrop> = None;
};

fn main() {
    let _reference = RETURNED_REFERENCE;
    let _value = RETURNED;
    unsafe {
        assert_eq!(DROPS, 0);
    }
    drop(_value);
    unsafe {
        assert_eq!(DROPS, 1);
    }
}
