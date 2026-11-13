macro_rules! twice {
    ($action:stmt) => {{
        $action;
        $action;
    }};
}

fn main() {
    let mut value = 3u32;
    twice!(value = value * 2 + 1);
    assert_eq!(value, 15);
}
