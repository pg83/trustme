macro_rules! assemble {
    ($( $line:stmt ; )* => $value:expr) => {{
        $( $line; )*
        $value
    }};
    ($($other:tt)*) => { 5u16 };
}

fn main() {
    let sum = assemble!(
        let left: u16 = 79;
        let right: u16 = 83;
        => left + right
    );
    assert_eq!(sum, 162);
    assert_eq!(assemble!(unused), 5);
}
