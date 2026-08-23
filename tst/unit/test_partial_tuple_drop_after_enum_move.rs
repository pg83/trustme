//@ edition: 2021

#![allow(static_mut_refs)]

static mut LOG: u32 = 0;

struct Dropped(u32);

impl Drop for Dropped {
    fn drop(&mut self) {
        unsafe { LOG = LOG * 10 + self.0 };
    }
}

fn main() {
    {
        let mut value = None;
        match value {
            None => {}
            _ => return,
        }

        *(&mut value) = Some((Dropped(1), Dropped(2)));
        match value {
            Some((_moved, _)) => {}
            None => {}
        }
    }

    assert_eq!(unsafe { LOG }, 12);
}
