// async-trait's `#[async_trait] pub trait ManageConnection { type Connection:
// Send + 'static; .. }`: the macro is handed the trait with the bounds of its
// associated types, which our printer refused to write.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item_spelled;

#[echo_item_spelled("'static")]
trait Manage {
    type Connection: Send + Sync + ?Sized + 'static;
    fn get(&self) -> &Self::Connection;
}

struct Pool(u8);

impl Manage for Pool {
    type Connection = u8;
    fn get(&self) -> &u8 {
        &self.0
    }
}

fn main() {
    assert_eq!(*Pool(4).get(), 4);
}
