//@ compile-only

union Overlay {
    flag: bool,
    byte: u8,
}

fn main() {
    let value = Overlay { byte: 0 };
    let _ = unsafe {
        match value {
            Overlay { flag: true } => 1u8,
            Overlay { byte: 1..=3 } => 2,
            _ => 0,
        }
    };
}
