//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// smol's examples write `#[apply(main!)] async fn main(ex: &Arc<Executor>)`,
// and smol-macros' `main!` matches `($ex:ident : &$exty:ty)` - no comma after
// the parameter. Upstream hands an attribute macro the parameter list as it was
// written; we wrote a comma after every parameter, the last one too, and no arm
// of `main!` matched.
extern crate proc_macro_item_passthrough;
use proc_macro_item_passthrough::echo_item_spelled_compact;

#[echo_item_spelled_compact("fn sum(a: u32, b: u32) -> u32")]
fn sum(a: u32, b: u32) -> u32 {
    a + b
}

#[echo_item_spelled_compact("fn diff(a: u32, b: u32,) -> u32")]
fn diff(a: u32, b: u32,) -> u32 {
    a - b
}

struct Counter(u32);

impl Counter {
    #[echo_item_spelled_compact("fn get(&self) -> u32")]
    fn get(&self) -> u32 {
        self.0
    }
}

fn main() {
    assert_eq!(sum(2, 3) + diff(5, 1) + Counter(1).get(), 10);
}
