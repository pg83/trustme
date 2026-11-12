#![feature(never_patterns)]

enum Empty {}

fn classify(value: Option<Empty>) -> u16 {
    match value {
        (Some(!) | Some(!)),
        None => 941,
    }
}

fn main() {
    assert_eq!(classify(None), 941);
}
