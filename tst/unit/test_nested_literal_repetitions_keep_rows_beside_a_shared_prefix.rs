macro_rules! rows {
    ($( $( ping )* pong $value:literal )* ping finish) => {
        [$( $value as u16 ),*]
    };
}

fn main() {
    let empty: [u16; 0] = rows!(ping finish);
    let values = rows!(pong 17 ping pong 23 ping ping pong 31 ping finish);
    let single = rows!(ping ping pong 43 ping finish);
    assert!(empty.is_empty());
    assert_eq!(values, [17, 23, 31]);
    assert_eq!(single, [43]);
}
