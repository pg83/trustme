//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
use proc_macro_item_passthrough::echo_item_spelled_compact;
macro_rules! checked {
    ($spelling:literal; $item:item) => {
        #[echo_item_spelled_compact($spelling)]
        $item
    };
}
checked! { "<'data:'view+'scope,'view:'scope,'scope>";
    fn suffix<'data: 'view + 'scope, 'view: 'scope, 'scope>(data: &'data [u8], _: &'view (), _: &'scope ()) -> &'scope [u8] {
        &data[1..]
    }
}
checked! { "where'data:'view,'view:'scope";
    fn prefix<'data, 'view, 'scope>(data: &'data [u8], _: &'view (), _: &'scope ()) -> &'scope [u8]
    where 'data: 'view, 'view: 'scope {
        &data[..2]
    }
}
fn main() {
    let bytes = [3, 5, 8, 13];
    let shorter = ();
    assert_eq!(suffix(&bytes, &shorter, &()), &[5, 8, 13]);
    assert_eq!(prefix(&bytes, &shorter, &()), &[3, 5]);
}
