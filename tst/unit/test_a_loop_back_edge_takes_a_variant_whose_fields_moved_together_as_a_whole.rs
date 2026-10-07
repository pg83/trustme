// hickory-proto 0.26's CAA read_issuer: a state machine `match state { .. }` in a
// loop whose arms move a variant's fields out and assign `state` again on every
// path. At the back edge each droppable field of a variant carries its own drop
// flag; the loop head tracks the variant as one, and takes it from them.
use std::cell::Cell;

thread_local! {
    static LIVE: Cell<isize> = Cell::new(0);
}

struct Tracked(String);

impl Tracked {
    fn new(text: &str) -> Self {
        LIVE.with(|live| live.set(live.get() + 1));
        Tracked(text.to_string())
    }
}

impl Drop for Tracked {
    fn drop(&mut self) {
        LIVE.with(|live| live.set(live.get() - 1));
    }
}

enum State {
    Before(Vec<Tracked>),
    Key { first: bool, key: Tracked, done: Vec<Tracked> },
}

fn parse(bytes: &[u8]) -> Result<usize, String> {
    let mut state = State::Before(vec![]);
    for ch in bytes {
        match state {
            State::Before(done) => {
                if *ch == b';' {
                    state = State::Before(done);
                } else {
                    state = State::Key { first: true, key: Tracked::new(""), done };
                }
            }
            State::Key { first, mut key, mut done } => match char::from(*ch) {
                ';' => {
                    done.push(key);
                    state = State::Before(done);
                }
                c if !first || c != '-' => {
                    key.0.push(c);
                    state = State::Key { first: false, key, done };
                }
                c => return Err(format!("bad {c}")),
            },
        }
    }
    Ok(match state {
        State::Before(done) => done.len(),
        State::Key { done, .. } => done.len() + 1,
    })
}

fn main() {
    assert_eq!(parse(b"ab;cd;"), Ok(2));
    assert_eq!(parse(b"ab;c"), Ok(2));
    assert_eq!(parse(b"a-"), Err(String::from("bad -")));
    assert_eq!(parse(b";-"), Ok(1));
    assert_eq!(LIVE.with(|live| live.get()), 0);
}
