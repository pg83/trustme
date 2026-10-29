fn main() {
    let failure = std::panic::catch_unwind(|| assert!({
        let [first, second] = [3u8, 5];
        first == second
    })).unwrap_err();
    let message = failure.downcast_ref::<String>().map(String::as_str)
        .or_else(|| failure.downcast_ref::<&str>().copied()).unwrap();
    let compact: String = message.split_whitespace().collect();
    assert!(compact.contains("let[first,second]=[3u8,5]"));
}
