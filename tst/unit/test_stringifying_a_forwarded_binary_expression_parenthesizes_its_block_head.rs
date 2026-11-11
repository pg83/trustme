macro_rules! show {
    ($block:expr) => {{
        let spelling = stringify!($block);
        assert!(spelling.contains("127"));
        assert!(spelling.contains('+'));
        $block
    }};
}

macro_rules! preserve {
    ($whole:expr) => { show!({ $whole }) };
}

macro_rules! add {
    ($left:expr) => { preserve!($left + 2u16) };
}

fn main() {
    assert_eq!(add!({ 127u16 }), 129u16);
}
