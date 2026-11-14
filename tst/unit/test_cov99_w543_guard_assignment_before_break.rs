//@ edition: 2024
#![feature(if_let_guard)]

fn score(candidate: Option<u32>, enabled: bool) -> u32 {
    loop {
        match () {
            () if let Some(mut points) = candidate
                && enabled
                && { points = points * 3; points <= 18 } => break points,
            () => break 0,
        }
    }
}

fn main() {
    assert_eq!(score(Some(4), true), 12);
    assert_eq!(score(Some(7), true), 0);
    assert_eq!(score(Some(4), false), 0);
    assert_eq!(score(None, true), 0);
}
