// Extracted from library/core/src/sync/exclusive.rs:48
#![allow(unused)]
#![feature(exclusive_wrapper)]
fn main() {
    use core::cell::Cell;
    use core::sync::Exclusive;
    
    async fn other() {}
    fn assert_sync<T: Sync>(t: T) {}
    struct State<F> {
        future: Exclusive<F>
    }
    
    assert_sync(State {
        future: Exclusive::new(async {
            let cell = Cell::new(1);
            let cell_ref = &cell;
            other().await;
            let value = cell_ref.get();
        })
    });
}
