#![feature(try_blocks)]

macro_rules! step { ($value:expr) => { $value? }; }

fn compute(success: bool) -> Result<u16, u8> {
    try {
        let first = step!(if success { Ok(13u16) } else { Err(7u8) });
        let nested: Result<u16, u8> = try { step!(Ok::<u16, u8>(first + 3)) + 2 };
        step!(nested) * 2
    }
}

fn main() {
    assert_eq!(compute(true), Ok(36));
    assert_eq!(compute(false), Err(7));
}
