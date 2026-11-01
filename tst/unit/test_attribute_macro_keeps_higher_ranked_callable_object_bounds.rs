//@ proc-macro-aux-build: preserve_bound_types.rs
use preserve_bound_types::preserve;

#[preserve]
fn apply<'a>(function: &(dyn for<'b> Fn(&'b str) -> usize + Send + Sync + 'a), value: &str) -> usize {
    function(value)
}

fn main() {
    let bias = 7usize;
    let function = |text: &str| text.len() + bias;
    assert_eq!(apply(&function, "four"), 11);
    assert_eq!(apply(&function, "ab"), 9);
}
