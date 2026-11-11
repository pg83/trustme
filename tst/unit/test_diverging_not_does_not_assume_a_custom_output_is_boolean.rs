//@ compile-only

struct Answer;

fn read() -> Answer {
    !(return Answer)
}

fn main() {}
