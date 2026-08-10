static mut DROP_LOG: [u32; 3] = [0; 3];
static mut DROP_COUNT: usize = 0;

struct RecordDrop(u32);

impl Drop for RecordDrop {
    fn drop(&mut self) {
        unsafe {
            DROP_LOG[DROP_COUNT] = self.0;
            DROP_COUNT += 1;
        }
    }
}

fn main() {
    let _ = RecordDrop(1);
    unsafe {
        assert_eq!(DROP_COUNT, 1);
        assert_eq!(DROP_LOG[0], 1);
    }

    _ = RecordDrop(2);
    unsafe {
        assert_eq!(DROP_COUNT, 2);
        assert_eq!(DROP_LOG[1], 2);
    }

    let _ = (RecordDrop(3),);
    unsafe {
        assert_eq!(DROP_COUNT, 3);
        assert_eq!(DROP_LOG[2], 3);
    }
}
