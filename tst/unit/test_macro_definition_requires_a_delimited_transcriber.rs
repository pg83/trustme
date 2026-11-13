//@ compile-fail: error:

macro_rules! undelimited_transcriber {
    () => 211u16
}

fn main() {}
