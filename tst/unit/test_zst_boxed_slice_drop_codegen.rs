static mut DROPS: i32 = 0;

struct DropMe;

impl Drop for DropMe {
    fn drop(&mut self) {
        unsafe {
            DROPS += 1;
        }
    }
}

fn main() {
    let items: Box<[DropMe]> = Box::new([DropMe]);
    drop(items);

    let nested: Box<[[DropMe; 2]; 2]> = Box::new([
        [DropMe, DropMe],
        [DropMe, DropMe],
    ]);
    drop(nested);

    assert_eq!(unsafe { DROPS }, 5);
}
