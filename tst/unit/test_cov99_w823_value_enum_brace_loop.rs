enum State {
    Ready,
    Done,
}

fn classify(state: State) -> u32 {
    loop {
        match state {
            State::Ready {} => break 23,
            State::Done => break 47,
        }
    }
}

fn main() {
    assert_eq!(classify(State::Ready), 23);
    assert_eq!(classify(State::Done), 47);
}
