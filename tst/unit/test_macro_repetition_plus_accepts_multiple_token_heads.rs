macro_rules! records {
    ($( $(#[$attribute:meta])* struct $name:ident; ),+) => {
        $( $(#[$attribute])* struct $name; )+
    };
}
records!(struct First;, #[derive(Clone, Copy)] struct Second;, #[allow(dead_code)] struct Third;);
fn main() {
    let first = First;
    let second = Second;
    let third = Third;
    assert_eq!(core::mem::size_of_val(&first), 0);
    assert_eq!(core::mem::size_of_val(&second.clone()), 0);
    assert_eq!(core::mem::size_of_val(&third), 0);
}
