static mut DROPS: usize = 0;

struct CountDrop;

impl Drop for CountDrop {
    fn drop(&mut self) {
        unsafe {
            DROPS += 1;
        }
    }
}

fn observe_after_for(_: ()) {
    unsafe {
        assert_eq!(DROPS, 3);
    }
}

fn main() {
    for _ in &[CountDrop] {
        unsafe {
            assert_eq!(DROPS, 0);
        }
    }

    unsafe {
        assert_eq!(DROPS, 1);
    }

    let _: () = for _ in &[CountDrop] {
        unsafe {
            assert_eq!(DROPS, 1);
        }
    };
    unsafe {
        assert_eq!(DROPS, 2);
    }

    observe_after_for(for _ in &[CountDrop] {
        unsafe {
            assert_eq!(DROPS, 2);
        }
    });
}
