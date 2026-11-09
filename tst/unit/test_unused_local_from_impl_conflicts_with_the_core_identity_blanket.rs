//@ compile-fail: conflicting implementations
struct Local;

impl From<Local> for Local {
    fn from(value: Local) -> Local {
        value
    }
}

fn main() {}
