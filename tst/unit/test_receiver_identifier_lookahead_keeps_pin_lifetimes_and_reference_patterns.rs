#![feature(pin_ergonomics)]

use std::pin::Pin;

struct Source(u32);

fn plain(&value: &u32) -> u32 {
    value
}

impl Source {
    fn read(&pin const self) -> u32 {
        Pin::get_ref(self).0
    }

    fn increase<'a>(&'a pin mut self, amount: u32) -> u32 {
        let value = Pin::get_mut(self);
        value.0 += amount;
        value.0
    }
}

macro_rules! install {
    () => {
        fn read_macro<'a>(&'a pin const self) -> u32 {
            Pin::get_ref(self).0
        }
    };
}

impl Source {
    install!();
}

fn main() {
    assert_eq!(plain(&17), 17);
    let mut value = Source(61);
    assert_eq!(Pin::new(&value).read(), 61);
    let mut pinned = Pin::new(&mut value);
    assert_eq!(pinned.as_mut().increase(6), 67);
    assert_eq!(pinned.as_ref().read_macro(), 67);
}
