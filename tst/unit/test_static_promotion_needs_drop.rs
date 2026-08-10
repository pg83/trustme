static mut DROPS: usize = 0;

struct CountDrop;

impl Drop for CountDrop {
    fn drop(&mut self) {
        unsafe {
            DROPS += 1;
        }
    }
}

fn main() {
    {
        let _borrow = &[CountDrop];
    }
    unsafe {
        assert_eq!(DROPS, 1);
    }

    {
        let _field = &&(CountDrop, 0).1;
    }
    unsafe {
        assert_eq!(DROPS, 2);
    }
}
