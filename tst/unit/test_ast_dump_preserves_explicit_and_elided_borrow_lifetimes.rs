//@ compile-flags: -Zdump-ast

#[derive(Debug)]
struct Message<'a> {
    text: &'a str,
    suffix: &'static str,
}

fn render(message: &Message<'_>) -> String {
    format!("{}{}", message.text, message.suffix)
}

fn main() {
    let owned = String::from("borrowed");
    let message = Message { text: &owned, suffix: "!" };
    assert_eq!(render(&message), "borrowed!");
    assert!(format!("{:?}", message).contains("borrowed"));
}
