macro_rules! leaf {
    ($name:ident, $increment:expr) => {{
        let value = 200u32;
        $name + $increment + (value - 200)
    }};
}

macro_rules! layer {
    ($name:ident) => {{
        let value = 17u32;
        (leaf!($name, 3u32), leaf!(value, 5u32))
    }};
}

fn main() {
    let value = 40u32;
    assert_eq!(layer!(value), (43u32, 22u32));
    assert_eq!(value, 40u32);
}
