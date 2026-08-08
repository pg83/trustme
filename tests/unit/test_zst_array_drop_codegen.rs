static mut DROPS: i32 = 0;

#[repr(align(64))]
struct DropMe;

impl Drop for DropMe {
    fn drop(&mut self) {
        assert_eq!(self as *mut DropMe as usize % 64, 0);
        unsafe {
            DROPS += 1;
        }
    }
}

fn main() {
    {
        let _items = [DropMe, DropMe, DropMe];
    }

    assert_eq!(unsafe { DROPS }, 3);
}
