//@ compile-fail: range pattern
enum Edge {
    First,
    Last,
}

fn main() {
    match Edge::First {
        Edge::First..=Edge::Last => (),
        _ => (),
    }
}
