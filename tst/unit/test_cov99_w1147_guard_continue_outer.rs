//@ edition: 2021

fn guard_continue(value: u8) -> u8 {
    let mut turns = 0u8;
    'retry: loop {
        turns += 1;
        match value {
            0 | 1 if {
                if turns == 1 {
                    continue 'retry;
                }
                true
            } => return turns,
            _ => return 0,
        }
    }
}

fn main() {
    assert_eq!(guard_continue(0), 2);
    assert_eq!(guard_continue(1), 2);
    assert_eq!(guard_continue(2), 0);
}
