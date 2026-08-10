//@ compile-flags: -O

static mut DROPS: usize = 0;

struct CountDrop;

impl Drop for CountDrop {
    fn drop(&mut self) {
        unsafe {
            DROPS += 1;
        }
    }
}

fn maybe_create(create: bool) {
    let value;
    if create {
        value = CountDrop;
    }
}

fn main() {
    maybe_create(false);
    unsafe {
        assert_eq!(DROPS, 0);
    }

    maybe_create(true);
    unsafe {
        assert_eq!(DROPS, 1);
    }
}
