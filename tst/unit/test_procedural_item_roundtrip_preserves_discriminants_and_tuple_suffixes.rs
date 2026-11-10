//@ proc-macro-aux-build: own_item_tokens_passthrough.rs

macro_rules! round_trip {
    ($($item:item)*) => {
        own_item_tokens_passthrough::preserve!($($item)*);
    };
}

round_trip! {
    #[repr(i16)]
    enum Code {
        Low = -7,
        Next,
        High = 5 + 8,
    }

    fn ends((first, .., last): (u16, u16, u16, u16)) -> u16 {
        first + last
    }
}

fn main() {
    assert_eq!(Code::Low as i16, -7);
    assert_eq!(Code::Next as i16, -6);
    assert_eq!(Code::High as i16, 13);
    assert_eq!(ends((11, 0, 0, 31)), 42);
}
