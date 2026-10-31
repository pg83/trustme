enum State { Count(u8), Pair { left: u8, right: u8 }, Empty }
fn count() -> u16 {
    let state = State::Count(37);
    match state { State::Count(value) => value as u16 + 1, State::Pair { left, right } => left as u16 + right as u16, State::Empty => 0 }
}
fn pair() -> u16 {
    let state = State::Pair { left: 17, right: 23 };
    match state { State::Count(value) => value as u16 + 1, State::Pair { left, right } => left as u16 + right as u16, State::Empty => 0 }
}
fn empty() -> u16 {
    let state = State::Empty;
    match state { State::Count(value) => value as u16 + 1, State::Pair { left, right } => left as u16 + right as u16, State::Empty => 0 }
}
fn main() { assert_eq!((count(), pair(), empty()), (38, 40, 0)); }
