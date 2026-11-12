//@ input: aux/escaped_carriage_return_without_line_feed.bin
//@ compile-fail: bare CR

#[path = "aux/escaped_carriage_return_without_line_feed.bin"]
mod invalid;

fn main() {
    assert_eq!(invalid::VALUE, "leftright");
}
