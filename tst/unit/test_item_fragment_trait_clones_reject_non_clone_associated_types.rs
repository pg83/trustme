//@ compile-fail: Clone

macro_rules! copies {
    ($definition:item) => {
        mod left { $definition }
        mod right { $definition }
    };
}

copies! {
    pub trait Source {
        type Item: Clone;
    }
}

struct NotClone;
struct Value;

impl left::Source for Value {
    type Item = NotClone;
}

impl right::Source for Value {
    type Item = NotClone;
}

fn main() {}
