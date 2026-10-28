//@ compile-flags: --cfg 'escaped="q\"\\\n\r\t\0\x41"'

fn main() {
    assert!(cfg!(escaped = "q\"\\\n\r\t\0A"));
    assert!(!cfg!(escaped = "q\"\\\n\r\t\0B"));
    assert!(!cfg!(escaped = "q\"\\\n\r\t"));
    assert!(!cfg!(escaped = "plain"));
}
