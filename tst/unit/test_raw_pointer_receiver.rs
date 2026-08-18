// A `*mut T` receiver is taken as the `*const T` a method was written for:
// that is a cast, not a borrow. A method on the mutable pointer still wins.
trait Shared {
    fn which(self) -> u32;
}

impl Shared for *const u16 {
    fn which(self) -> u32 {
        1
    }
}

trait Both {
    fn which(self) -> u32;
}

impl Both for *const u8 {
    fn which(self) -> u32 {
        2
    }
}

impl Both for *mut u8 {
    fn which(self) -> u32 {
        3
    }
}

fn main() {
    let mut data = 3u16;
    let mut_ptr = std::ptr::addr_of_mut!(data);
    assert_eq!(mut_ptr.which(), 1);

    let mut byte = 1u8;
    let byte_ptr = std::ptr::addr_of_mut!(byte);
    assert_eq!(byte_ptr.which(), 3);
    assert_eq!((byte_ptr as *const u8).which(), 2);
}
