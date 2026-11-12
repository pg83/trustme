//@ compile-fail: error:

macro_rules! bind {
    ($pattern:pat) => {
        let ref $pattern = 311u16;
    };
}

fn main() {
    bind!(value);
}
