const VALUES: [&[u8]; 3] = [
    unsafe { std::slice::from_raw_parts(b"birch".as_ptr().add(1), 3) },
    b"cedar",
    unsafe { std::slice::from_raw_parts(b"maple".as_ptr().add(2), 2) },
];

fn main() {
    let values = std::hint::black_box(VALUES);
    assert_eq!(values, [b"irc" as &[u8], b"cedar", b"pl"]);
}
