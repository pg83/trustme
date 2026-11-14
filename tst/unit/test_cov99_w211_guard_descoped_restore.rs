//@ edition: 2024
#![feature(if_let_guard)]

fn guarded_return(flag: bool) -> u32 {
    match flag {
        true if let Some(mut value) = Some(1u32)
            && value == 1
            && { value = 2; value == 2 } => return value,
        _ => 0,
    }
}

fn main() {
    assert_eq!(guarded_return(true), 2);
    assert_eq!(guarded_return(false), 0);
}
