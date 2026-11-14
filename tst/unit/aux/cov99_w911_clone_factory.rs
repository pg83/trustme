pub struct Stamp(u32);

impl Clone for Stamp {
    fn clone(&self) -> Self {
        if self.0 == 0 {
            Self(1)
        } else {
            Self(self.0)
        }
    }
}

impl Drop for Stamp {
    fn drop(&mut self) {
        self.0 = 0;
    }
}

impl Stamp {
    fn read(&self) -> u32 {
        self.0
    }
}

pub fn make(value: u32) -> impl Fn() -> u32 + Clone {
    let stamp = Stamp(value);
    move || stamp.read()
}
