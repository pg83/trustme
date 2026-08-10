//@ compile-flags: -O

static mut DROP_LOG: [u8; 4] = [0; 4];
static mut DROP_COUNT: usize = 0;

struct LogDrop(u8);

impl Drop for LogDrop {
    fn drop(&mut self) {
        unsafe {
            DROP_LOG[DROP_COUNT] = self.0;
            DROP_COUNT += 1;
        }
    }
}

#[inline(never)]
fn conditional_pair(create_first: bool, create_second: bool) {
    let first;
    if create_first {
        first = LogDrop(1);
    }

    let second;
    if create_second {
        second = LogDrop(2);
    }
}

fn main() {
    conditional_pair(false, false);
    conditional_pair(true, false);
    conditional_pair(false, true);
    conditional_pair(true, true);

    unsafe {
        assert_eq!(DROP_COUNT, 4);
        assert_eq!(DROP_LOG, [1, 2, 2, 1]);
    }
}
