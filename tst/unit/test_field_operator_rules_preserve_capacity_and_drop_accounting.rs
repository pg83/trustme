mod protocol {
    #[repr(u16)]
    pub enum Tag {
        Ping = 677,
        Pong = 683,
    }

    pub struct Frame {
        pub used: u16,
        pub capacity: u16,
        pub ready: bool,
    }

    impl Frame {
        pub fn free(&self) -> u16 {
            self.capacity - self.used
        }

        pub fn accept(&mut self, extra: u16) -> bool {
            self.used += extra;
            self.ready & (self.used <= self.capacity)
        }
    }
}

struct Guard<'a>(&'a mut u16);

impl Drop for Guard<'_> {
    fn drop(&mut self) {
        *self.0 += 11;
    }
}

fn main() {
    let mut frame = protocol::Frame { used: 17, capacity: 709, ready: true };
    assert_eq!(frame.free(), 692);
    assert!(frame.accept(7));
    assert_eq!(frame.free(), 685);
    assert_eq!(protocol::Tag::Ping as u16, 677);
    assert_eq!(protocol::Tag::Pong as u16, 683);

    let mut tally = 0;
    {
        let _guard = Guard(&mut tally);
    }
    assert_eq!(tally, 11);
}
