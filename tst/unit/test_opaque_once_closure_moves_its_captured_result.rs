fn make(prefix: String) -> impl FnOnce(&str) -> String {
    move |suffix| {
        let mut result = prefix;
        result.push_str(suffix);
        result
    }
}

fn main() {
    let call = make(String::from("north"));
    assert_eq!(call("west"), "northwest");
}
