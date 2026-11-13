macro_rules! members {
    () => {
        type Value;
        const BASE: u32 = 7;
        fn base(&self) -> u32 { Self::BASE }
    };
}

macro_rules! relay {
    ($item:item) => { $item };
}

trait Sample {
    members!();
    relay! { const EXTRA: u32 = 2; }

    #[cfg(any())]
    const HIDDEN: u32 = 100;

    fn read(&self) -> u32 {
        self.base() + Self::EXTRA
    }
}

struct Value;

impl Sample for Value {
    type Value = u32;
}

fn main() {
    assert_eq!(Value.read(), 9);
}
