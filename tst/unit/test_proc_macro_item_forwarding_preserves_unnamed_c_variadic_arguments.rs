//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo;

macro_rules! declare {
    ($foreign:item) => {
        echo! { $foreign }
    };
}

declare! {
    extern "C" {
        fn snprintf(buffer: *mut u8, length: usize, format: *const u8, ...) -> i32;
    }
}

fn main() {
    let mut buffer = [0u8; 16];
    let length = unsafe {
        snprintf(
            buffer.as_mut_ptr(),
            buffer.len(),
            b"%u/%x\0".as_ptr(),
            std::hint::black_box(61u32),
            std::hint::black_box(83u32),
        )
    };
    assert_eq!(length, 5);
    assert_eq!(&buffer[..6], b"61/53\0");
}
