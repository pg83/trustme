//@ edition: 2021
//@ compile-only
//@ compile-flags: -Cpanic=abort

union Bits {
    number: u8,
    flag: bool,
}

fn classify(value: Bits) -> u8 {
    unsafe {
        match value {
            Bits { number: 1..=3 } => 1,
            Bits { flag: true } => 2,
            _ => 0,
        }
    }
}

fn main() {
    let _ = classify(Bits { number: 1 });
}
