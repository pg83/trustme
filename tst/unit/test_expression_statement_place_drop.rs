#![allow(path_statements)]

static mut EVENTS: [u32; 4] = [0; 4];
static mut EVENT_COUNT: usize = 0;

struct RecordDrop(u32);

impl Drop for RecordDrop {
    fn drop(&mut self) {
        record(self.0);
    }
}

fn record(value: u32) {
    unsafe {
        EVENTS[EVENT_COUNT] = value;
        EVENT_COUNT += 1;
    }
}

fn main() {
    {
        let value = RecordDrop(1);
        value;
        record(2);
    }
    unsafe {
        assert_eq!(&EVENTS[..2], &[1, 2]);
    }

    {
        let value = Box::new(RecordDrop(3));
        *value;
        record(4);
    }
    unsafe {
        assert_eq!(EVENTS, [1, 2, 3, 4]);
    }
}
