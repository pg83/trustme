#![feature(linkage)]

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Packet(u16);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum State {
    Ready(Packet),
    Empty,
}

#[derive(Clone, Copy)]
union Word {
    value: u16,
}

#[linkage = "weak"]
static QUOTA: u16 = 821;

#[linkage = "external"]
fn next(value: u16) -> u16 {
    value + 7
}

fn main() {
    let state = State::Ready(Packet(next(QUOTA)));
    assert_eq!(state.clone(), State::Ready(Packet(828)));
    assert_ne!(state, State::Empty);
    let word = Word { value: 827 };
    let copied = word.clone();
    assert_eq!(unsafe { copied.value }, 827);
}
